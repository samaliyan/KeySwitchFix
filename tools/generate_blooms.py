#!/usr/bin/env python3
"""Reproduce dictionary Blooms and build prefix/common-word guards."""

import argparse
import hashlib
import json
import re
import struct
import unicodedata
from importlib import metadata
from pathlib import Path

WORD_HASH_COUNT = 9
PREFIX_HASH_COUNT = 14
COMMON_HASH_COUNT = 10
COMMON_PREFIX_HASH_COUNT = 14
MINIMUM_PREFIX = 3
ENGLISH_BITS = 1 << 21
PERSIAN_BITS = 1 << 22
PREFIX_BITS = 1 << 21
ENGLISH_COMMON_BITS = 1 << 20
PERSIAN_COMMON_BITS = 1 << 20
FREQUENT_BITS = 1 << 17
COMMON_PREFIX_BITS = 1 << 20
ENGLISH_COMMON_LIMIT = 20000
ENGLISH_FREQUENT_LIMIT = 2000
PERSIAN_COMMON_LIMIT = 20000
PERSIAN_FREQUENT_LIMIT = 2000
WORDFREQ_ENGLISH_SHA256 = (
    "dffae8066b78dce0a6667cf5f58e567054f902674667090a7ac8a8a44628b05c"
)
WORDFREQ_PERSIAN_SHA256 = (
    "bfb503f6b0d6bdddce720ee8154faf1c79d8b0417e6c865643da3b17b7638057"
)

ENGLISH_TO_PERSIAN = {
    "a": "ش", "b": "ذ", "c": "ز", "d": "ی", "e": "ث", "f": "ب",
    "g": "ل", "h": "ا", "i": "ه", "j": "ت", "k": "ن", "l": "م",
    "m": "پ", "n": "د", "o": "خ", "p": "ح", "q": "ض", "r": "ق",
    "s": "س", "t": "ف", "u": "ع", "v": "ر", "w": "ص", "x": "ط",
    "y": "غ", "z": "ظ", "'": "گ",
}
# Keep these two sets identical to short_english_word()/short_persian_word()
# in src/core.c. Every English token whose physical keys spell a listed
# Persian word (id/هی, ms/پس, pr/حق) must be on the English side too, so it
# is treated as a collision instead of one-sided Persian evidence.
SHORT_ENGLISH_WORDS = {
    "a", "i",
    "ad", "ah", "ai", "am", "an", "as", "at", "be", "by", "do", "go",
    "he", "hi", "id", "if", "in", "is", "it", "me", "ms", "my", "no",
    "of", "oh", "ok", "on", "or", "pr", "so", "to", "up", "us", "we",
}
# می, ها, تر, ام, ات are the halves around a ZWNJ (Shift+Space).
SHORT_PERSIAN_WORDS = {
    "و",
    "آب", "آن", "آه", "از", "ام", "او", "ای", "ات", "با", "بد", "بر",
    "به", "بی", "پا", "پر", "پس", "تا", "تب", "تر", "ته", "تو", "جا",
    "جز", "چه", "خب", "خط", "در", "دل", "دم", "ده", "دو", "را", "رو",
    "زن", "سر", "سن", "سه", "شب", "شد", "حق", "حل", "کم", "کن", "که",
    "کل", "کی", "گل", "لب", "ما", "من", "می", "نه", "نو", "ها", "هم",
    "هر", "هی", "یا", "یک", "وی",
}

# A small number of malformed fragments exist in the upstream Persian
# stop-word aggregation. Keep the source reproducible without promoting those
# fragments to accepted words.
PERSIAN_COMMON_REJECT = {
    "استفاد",
    "بارة",
    "بعری",
    "تول",
    "خیاه",
    "روب",
    "هبچ",
    "وگو",
    "پاعین",
}


def hash_positions(value: str, bit_mask: int, hash_count: int):
    first = 2166136261
    second = 5381
    for byte in value.encode("utf-8"):
        first = ((first ^ byte) * 16777619) & 0xFFFFFFFF
        second = (((second << 5) + second) ^ byte) & 0xFFFFFFFF
    second = ((second << 1) | 1) & 0xFFFFFFFF
    for index in range(hash_count):
        yield (
            first
            + index * second
            + index * index * 0x9E3779B9
        ) & bit_mask


def build_bloom(words, bit_count: int, hash_count: int) -> bytes:
    bits = bytearray(bit_count // 8)
    mask = bit_count - 1
    for word in words:
        for bit in hash_positions(word, mask, hash_count):
            bits[bit >> 3] |= 1 << (bit & 7)
    return b"KSWB" + struct.pack("<III", 1, bit_count, hash_count) + bits


def dictionary_lines(package: Path):
    return (package / "index.dic").read_text(encoding="utf-8-sig").splitlines()[1:]


def english_words(package: Path):
    result = set()
    pattern = re.compile(r"[a-z]+(?:'[a-z]+)?")
    for line in dictionary_lines(package):
        word = line.split("/", 1)[0].lower()
        if len(word) >= MINIMUM_PREFIX and pattern.fullmatch(word):
            result.add(word)
    return result


def normalize_persian(word: str) -> str:
    word = (
        word.replace("ي", "ی")
        .replace("ى", "ی")
        .replace("ك", "ک")
        .replace("\u200c", "")
    )
    return "".join(
        character
        for character in unicodedata.normalize("NFC", word)
        if unicodedata.category(character) != "Mn"
    )


def persian_words(package: Path):
    result = set()
    for line in dictionary_lines(package):
        word = normalize_persian(line.split("/", 1)[0])
        if len(word) >= MINIMUM_PREFIX and word.isalpha():
            result.add(word)
    return result


def proper_prefixes(words):
    return {
        word[:length]
        for word in words
        for length in range(MINIMUM_PREFIX, len(word))
    }


def wordfreq_words(language: str, limit: int):
    from wordfreq import iter_wordlist

    result = []
    seen = set()
    english_pattern = re.compile(r"[a-z]+(?:'[a-z]+)?")
    for raw_word in iter_wordlist(language):
        if language == "en":
            word = raw_word.lower()
            accepted = bool(english_pattern.fullmatch(word))
        else:
            # ZWNJ cannot occur inside the app's physical-key token. Removing
            # it would create a synthetic spelling, so reject it instead.
            if "\u200c" in raw_word:
                continue
            word = normalize_persian(raw_word)
            accepted = word.isalpha() and word not in PERSIAN_COMMON_REJECT
        if len(word) >= MINIMUM_PREFIX and accepted and word not in seen:
            seen.add(word)
            result.append(word)
            if len(result) == limit:
                break
    if len(result) != limit:
        raise SystemExit(
            f"wordfreq {language}: expected {limit} eligible words, "
            f"found {len(result)}"
        )
    return result


def validate_wordfreq():
    from wordfreq import __file__ as module_file

    if metadata.version("wordfreq") != "3.1.1":
        raise SystemExit("wordfreq: expected 3.1.1")
    data_directory = Path(module_file).resolve().parent / "data"
    expected = {
        "large_en.msgpack.gz": WORDFREQ_ENGLISH_SHA256,
        "small_fa.msgpack.gz": WORDFREQ_PERSIAN_SHA256,
    }
    for filename, expected_digest in expected.items():
        actual = hashlib.sha256((data_directory / filename).read_bytes()).hexdigest()
        if actual != expected_digest:
            raise SystemExit(f"wordfreq data hash mismatch: {filename}")


def physical_persian(english: str):
    try:
        return "".join(ENGLISH_TO_PERSIAN[character] for character in english)
    except KeyError:
        return None


def write_collision_priors(
    path: Path,
    english_words_set,
    persian_words_set,
):
    from wordfreq import zipf_frequency

    entries = []
    for english in sorted(english_words_set):
        persian = physical_persian(english)
        if not persian or persian not in persian_words_set:
            continue
        english_zipf = zipf_frequency(english, "en")
        persian_zipf = zipf_frequency(persian, "fa")
        if english_zipf <= 0.0 or persian_zipf <= 0.0:
            # Missing frequency evidence is neutral. It must never become an
            # extreme preference merely because one corpus omitted a word.
            points = 0
        else:
            # wordfreq's Zipf values are already normalized per language:
            # one point is one base-10 order of magnitude per billion words.
            points = round(24.0 * (persian_zipf - english_zipf))
            points = max(-75, min(75, points))
        entries.append((english, points))

    lines = [
        "/* Generated by tools/generate_blooms.py; do not edit manually. */",
        "static const KS_COLLISION_PRIOR KS_COLLISION_PRIORS[] = {",
    ]
    lines.extend(
        f'    {{L"{word}", {points}}},' for word, points in entries
    )
    lines.extend([
        "};",
        (
            "static const size_t KS_COLLISION_PRIOR_COUNT = "
            "sizeof(KS_COLLISION_PRIORS) / sizeof(KS_COLLISION_PRIORS[0]);"
        ),
        "",
    ])
    path.write_text("\n".join(lines), encoding="utf-8")
    return len(entries)


def require_version(package: Path, expected: str):
    metadata = json.loads((package / "package.json").read_text(encoding="utf-8"))
    actual = metadata.get("version")
    if actual != expected:
        raise SystemExit(f"{package.name}: expected {expected}, found {actual}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--dictionary-root",
        type=Path,
        required=True,
        help="Directory containing dictionary-en and dictionary-fa npm packages",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "resources",
    )
    parser.add_argument(
        "--prior-output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "src" / "collision_priors.inc",
    )
    arguments = parser.parse_args()

    english_package = arguments.dictionary_root / "dictionary-en"
    persian_package = arguments.dictionary_root / "dictionary-fa"
    require_version(english_package, "4.0.0")
    require_version(persian_package, "2.0.0")
    validate_wordfreq()

    english = english_words(english_package)
    persian = persian_words(persian_package)
    english_bloom = build_bloom(english, ENGLISH_BITS, WORD_HASH_COUNT)
    persian_bloom = build_bloom(persian, PERSIAN_BITS, WORD_HASH_COUNT)

    expected_english = (arguments.output_dir / "en.bloom").read_bytes()
    expected_persian = (arguments.output_dir / "fa.bloom").read_bytes()
    if english_bloom != expected_english or persian_bloom != expected_persian:
        raise SystemExit("Dictionary normalization no longer reproduces existing Bloom files")

    english_prefixes = proper_prefixes(english)
    persian_prefixes = proper_prefixes(persian)
    (arguments.output_dir / "en-prefix.bloom").write_bytes(
        build_bloom(english_prefixes, PREFIX_BITS, PREFIX_HASH_COUNT)
    )
    (arguments.output_dir / "fa-prefix.bloom").write_bytes(
        build_bloom(persian_prefixes, PREFIX_BITS, PREFIX_HASH_COUNT)
    )
    english_common = set(wordfreq_words("en", ENGLISH_COMMON_LIMIT))
    english_frequent = set(wordfreq_words("en", ENGLISH_FREQUENT_LIMIT))
    persian_common = set(wordfreq_words("fa", PERSIAN_COMMON_LIMIT))
    persian_frequent = set(wordfreq_words("fa", PERSIAN_FREQUENT_LIMIT))
    english_frequent.update(SHORT_ENGLISH_WORDS)
    persian_frequent.update(SHORT_PERSIAN_WORDS)
    (arguments.output_dir / "en-common.bloom").write_bytes(
        build_bloom(english_common, ENGLISH_COMMON_BITS, COMMON_HASH_COUNT)
    )
    (arguments.output_dir / "fa-common.bloom").write_bytes(
        build_bloom(persian_common, PERSIAN_COMMON_BITS, COMMON_HASH_COUNT)
    )
    (arguments.output_dir / "en-frequent.bloom").write_bytes(
        build_bloom(english_frequent, FREQUENT_BITS, COMMON_HASH_COUNT)
    )
    (arguments.output_dir / "fa-frequent.bloom").write_bytes(
        build_bloom(persian_frequent, FREQUENT_BITS, COMMON_HASH_COUNT)
    )
    (arguments.output_dir / "en-common-prefix.bloom").write_bytes(
        build_bloom(
            proper_prefixes(english_common),
            COMMON_PREFIX_BITS,
            COMMON_PREFIX_HASH_COUNT,
        )
    )
    (arguments.output_dir / "fa-common-prefix.bloom").write_bytes(
        build_bloom(
            proper_prefixes(persian_common),
            COMMON_PREFIX_BITS,
            COMMON_PREFIX_HASH_COUNT,
        )
    )
    collision_count = write_collision_priors(
        arguments.prior_output,
        english | english_common | SHORT_ENGLISH_WORDS,
        persian | persian_common | SHORT_PERSIAN_WORDS,
    )
    print(
        "Generated guards:",
        f"{len(english_prefixes)} English,",
        f"{len(persian_prefixes)} Persian prefixes;",
        f"{len(english_common)} common English,",
        f"{len(persian_common)} common Persian words;",
        f"{collision_count} exact collision priors.",
    )


if __name__ == "__main__":
    main()
