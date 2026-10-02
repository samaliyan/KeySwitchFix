#!/usr/bin/env python3
"""Build KeySwitchFix language packs (.kslang) and the installer bundle.

A language pack lets KeySwitchFix repair words typed on the wrong keyboard
layout between any two languages, not only English and Persian. It holds the
language's dictionary as Bloom filters (all words, the 20,000 and 2,000 most
frequent, and the beginnings of words) plus its one- and two-letter words.

Words come from wordfreq (pip install wordfreq==3.1.1), or from a plain word
list (one word per line, most frequent first) for a language wordfreq does
not cover:

    python3 tools/build_language_pack.py --languages ru de --out build/languages
    python3 tools/build_language_pack.py --code ka --word-list ka.txt \\
        --english-name Georgian --native-name ქართული --langid 0x37 --flags 0 --out build/languages
    python3 tools/build_language_pack.py --bundle build/languages --bundle-out build/languages.bundle

Every word is normalised exactly as src/core.c (ks_lookup_form) looks it up:
keep both in step. Adding a language to LANGUAGES below is all it takes for a
language wordfreq knows.
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_blooms import build_bloom  # noqa: E402  (same hash as the app)

# KS_PROFILE_* in src/core.h
CASED = 0x0001
ASCII_ONLY = 0x0002
APOSTROPHE = 0x0004
ARABIC_MARKS = 0x0008
PERSIAN_ALEF = 0x0010
HEBREW_POINTS = 0x0020
LATIN = 0x0040
NO_SHAPE = 0x0080

MAX_WORD = 32
MINIMUM_PREFIX = 3
WORD_LIMIT = 120_000
COMMON_LIMIT = 20_000
FREQUENT_LIMIT = 2_000
SHORT_LIMIT = 80
SHORT_SOURCE = 1_500        # short words are taken from this many top words
MINIMUM_WORDS = 5_000       # fewer is not a dictionary (the test fixtures lower it)

# code: (English name, native name, Windows primary language ids, flags)
LANGUAGES = {
    "ar": ("Arabic", "العربية", [0x01], ARABIC_MARKS),
    "bg": ("Bulgarian", "Български", [0x02], CASED),
    "de": ("German", "Deutsch", [0x07], CASED | LATIN),
    "el": ("Greek", "Ελληνικά", [0x08], CASED),
    "es": ("Spanish", "Español", [0x0A], CASED | LATIN),
    "fr": ("French", "Français", [0x0C], CASED | LATIN | APOSTROPHE),
    "he": ("Hebrew", "עברית", [0x0D], HEBREW_POINTS),
    "it": ("Italian", "Italiano", [0x10], CASED | LATIN | APOSTROPHE),
    "nl": ("Dutch", "Nederlands", [0x13], CASED | LATIN),
    "pl": ("Polish", "Polski", [0x15], CASED | LATIN),
    "pt": ("Portuguese", "Português", [0x16], CASED | LATIN),
    "ru": ("Russian", "Русский", [0x19], CASED),
    "tr": ("Turkish", "Türkçe", [0x1F], CASED | LATIN),
    "uk": ("Ukrainian", "Українська", [0x22], CASED),
}

ARABIC_DIACRITICS = (
    set(range(0x064B, 0x0660)) | {0x0670} | set(range(0x06D6, 0x06DD))
    | set(range(0x06DF, 0x06E5)) | {0x06E7, 0x06E8} | set(range(0x06EA, 0x06EE))
)


def is_hebrew_point(c: int) -> bool:
    return 0x0591 <= c <= 0x05C7 and c not in (0x05BE, 0x05C0, 0x05C3, 0x05C6)


def is_letter(ch: str) -> bool:
    """Mirror of ks_is_letter() in src/core.c."""
    u = ord(ch)
    if ("a" <= ch <= "z") or ("A" <= ch <= "Z"):
        return True
    if 0x00C0 <= u <= 0x024F:
        return u not in (0x00D7, 0x00F7)
    if 0x0370 <= u <= 0x03FF:
        return not (u in (0x0375, 0x037E, 0x0384, 0x0385, 0x0387, 0x03F6, 0x038B, 0x038D, 0x03A2)
                    or 0x0378 <= u <= 0x0379 or 0x0380 <= u <= 0x0383)
    if 0x1F00 <= u <= 0x1FFF:
        return True
    if 0x0400 <= u <= 0x052F:
        return not (0x0482 <= u <= 0x0489)
    if 0x0531 <= u <= 0x0556 or 0x0561 <= u <= 0x0587:
        return True
    if 0x05D0 <= u <= 0x05EA or 0x05F0 <= u <= 0x05F2:
        return True
    if 0x0620 <= u <= 0x064A:
        return True
    if u in (0x066E, 0x066F, 0x06D5, 0x06EE, 0x06EF, 0x06FF) or 0x0671 <= u <= 0x06D3 or 0x06FA <= u <= 0x06FC:
        return True
    if 0x0900 <= u <= 0x0963 or 0x0971 <= u <= 0x097F:
        return True
    if 0x0E01 <= u <= 0x0E3A or 0x0E40 <= u <= 0x0E4E:
        return True
    if 0x10A0 <= u <= 0x10FF:
        return u != 0x10FB
    return False


def to_lower(ch: str) -> str:
    """Mirror of ks_to_lower() in src/core.c (deliberately not str.lower)."""
    u = ord(ch)
    if "A" <= ch <= "Z":
        return chr(u + 32)
    if 0x00C0 <= u <= 0x00DE and u != 0x00D7:
        return chr(u + 32)
    if u == 0x0130:
        return "i"
    if 0x0100 <= u <= 0x0137 and not u & 1:
        return chr(u + 1)
    if 0x0139 <= u <= 0x0148 and u & 1:
        return chr(u + 1)
    if 0x014A <= u <= 0x0177 and not u & 1:
        return chr(u + 1)
    if u == 0x0178:
        return "ÿ"
    if 0x0179 <= u <= 0x017E and u & 1:
        return chr(u + 1)
    if u == 0x0386:
        return "ά"
    if 0x0388 <= u <= 0x038A:
        return chr(u + 37)
    if u == 0x038C:
        return "ό"
    if u in (0x038E, 0x038F):
        return chr(u + 63)
    if 0x0391 <= u <= 0x03AB and u != 0x03A2:
        return chr(u + 32)
    if 0x0400 <= u <= 0x040F:
        return chr(u + 80)
    if 0x0410 <= u <= 0x042F:
        return chr(u + 32)
    if (0x0460 <= u <= 0x0481 or 0x048A <= u <= 0x04BF or 0x04D0 <= u <= 0x052F) and not u & 1:
        return chr(u + 1)
    if 0x0531 <= u <= 0x0556:
        return chr(u + 48)
    return ch


def lookup_form(word: str, flags: int):
    """Mirror of ks_lookup_form(); None when the word cannot be looked up."""
    out = []
    for ch in word:
        c = ord(ch)
        if flags & ARABIC_MARKS and c in ARABIC_DIACRITICS:
            continue
        if flags & HEBREW_POINTS and is_hebrew_point(c):
            continue
        if flags & CASED:
            if flags & ASCII_ONLY:
                ch = chr(ord(ch) + 32) if "A" <= ch <= "Z" else ch
            else:
                ch = to_lower(ch)
        out.append(ch)
    if not out or len(out) > MAX_WORD:
        return None
    form = "".join(out)
    if flags & NO_SHAPE:
        return form
    apostrophes = 0
    for index, ch in enumerate(form):
        letter = ("a" <= ch <= "z") if flags & ASCII_ONLY else is_letter(ch)
        if letter:
            continue
        if flags & APOSTROPHE and ch in ("'", "’") and not apostrophes and 0 < index < len(form) - 1:
            apostrophes = 1
            continue
        return None
    return form


def language_fixups(code: str, word: str):
    """wordfreq stores case-folded words; give back the forms people type."""
    forms = [word]
    if code == "el" and word.endswith("σ"):
        forms = [word[:-1] + "ς"]          # casefold turns final sigma into σ
    if code == "de" and "ss" in word:
        forms.append(word.replace("ss", "ß"))   # casefold turns ß into ss
    return forms


def wordfreq_source(code: str):
    try:
        from importlib import metadata
        from wordfreq import iter_wordlist
    except ImportError:
        raise SystemExit("wordfreq is not installed: pip install wordfreq==3.1.1")
    version = metadata.version("wordfreq")
    if version != "3.1.1":
        raise SystemExit(f"wordfreq: expected 3.1.1, found {version}")
    return iter_wordlist(code, wordlist="best")


def collect(code: str, flags: int, source):
    """Normalised words in frequency order, and the short words."""
    words = []
    seen = set()
    short = []
    for rank, raw in enumerate(source):
        raw = raw.strip()
        if not raw or raw.startswith("#"):
            continue
        for candidate in language_fixups(code, raw):
            form = lookup_form(candidate, flags)
            if form is None:
                continue
            if len(form) <= 2:
                if rank < SHORT_SOURCE and form not in short and len(short) < SHORT_LIMIT:
                    short.append(form)
                continue
            if form not in seen:
                seen.add(form)
                words.append(form)
        if len(words) >= WORD_LIMIT:
            break
    if len(words) < MINIMUM_WORDS:
        raise SystemExit(f"{code}: only {len(words)} usable words; a pack needs at least {MINIMUM_WORDS}")
    return words, short


def proper_prefixes(words):
    return {word[:length] for word in words for length in range(MINIMUM_PREFIX, len(word))}


def bloom_bits(count: int, bits_per_item: int) -> int:
    bits = 1 << 15
    while bits < count * bits_per_item:
        bits <<= 1
    return bits


def padded_utf8(text: str, size: int) -> bytes:
    # The app reads names as UTF-8 of the Basic Multilingual Plane only.
    if any(ord(ch) > 0xFFFF or 0xD800 <= ord(ch) <= 0xDFFF for ch in text):
        raise SystemExit(f"name has a character the app cannot show (outside the BMP): {text}")
    data = text.encode("utf-8")
    if len(data) >= size:
        raise SystemExit(f"name too long for the pack header (at most {size - 1} UTF-8 bytes): {text}")
    return data + b"\0" * (size - len(data))


BUILT_IN = ("en", "fa")
MAX_BUNDLE_PACKS = 64       # Setup accepts at most this many (src/installer.c)


def check_code(code: str) -> None:
    """The rule of ks_pack_parse_header: 2-7 of a-z 0-9 -, never a built-in."""
    if not 2 <= len(code) <= 7 or not all("a" <= c <= "z" or "0" <= c <= "9" or c == "-" for c in code):
        raise SystemExit(f"bad language code {code!r}: 2 to 7 characters of a-z, 0-9 and '-'")
    if code in BUILT_IN:
        raise SystemExit(f"{code} is built into KeySwitchFix; a pack for it would be ignored")


def check_langids(langids) -> None:
    if not langids or len(langids) > 4 or not all(0 < value <= 0x3FF for value in langids):
        raise SystemExit("--langid: one to four Windows primary language ids (0x001 to 0x3FF)")


def build_pack(code, english_name, native_name, langids, flags, source, model=0) -> bytes:
    words, short = collect(code, flags, source)
    common = words[:COMMON_LIMIT]
    frequent = words[:FREQUENT_LIMIT] + short
    prefixes = proper_prefixes(words)
    common_prefixes = proper_prefixes(common)
    sections = [
        (1, build_bloom(words, bloom_bits(len(words), 14), 9)),
        (2, build_bloom(common, bloom_bits(len(common), 14), 10)),
        (3, build_bloom(frequent, bloom_bits(len(frequent), 16), 10)),
        (4, build_bloom(prefixes, bloom_bits(len(prefixes), 9), 6)),
        (5, build_bloom(common_prefixes, bloom_bits(len(common_prefixes), 12), 8)),
        (6, "\n".join(short).encode("utf-8")),
    ]
    check_code(code)
    check_langids(langids)
    langid_field = (list(langids) + [0, 0, 0, 0])[:4]
    header = (
        b"KSLP"
        + struct.pack("<I", 1)
        + code.encode("ascii").ljust(8, b"\0")
        + struct.pack("<I", flags)
        + struct.pack("<4H", *langid_field)
        + struct.pack("<I", model)
        + padded_utf8(english_name, 48)
        + padded_utf8(native_name, 48)
        + struct.pack("<I", len(sections))
    )
    assert len(header) == 132
    offset = len(header) + 12 * len(sections)
    table = b""
    body = b""
    for kind, data in sections:
        table += struct.pack("<III", kind, offset + len(body), len(data))
        body += data
    print(f"{code}: {len(words)} words, {len(common)} common, {len(short)} short, "
          f"{len(prefixes)} prefixes, {(len(header) + len(table) + len(body)) // 1024} KB")
    return header + table + body


def write_bundle(directory: Path, output: Path):
    """All packs of a folder in one file for Setup to embed:
    "KSLB", u32 count, then { char name[24], u32 offset, u32 size } x count."""
    packs = sorted(directory.glob("*.kslang"))
    if len(packs) > MAX_BUNDLE_PACKS:
        raise SystemExit(f"{len(packs)} packs: Setup installs at most {MAX_BUNDLE_PACKS}")
    entries = b""
    body = b""
    start = 8 + 32 * len(packs)
    for pack in packs:
        check_code(pack.stem)   # Setup accepts only "<code>.kslang" (pack_file_name)
        name = pack.name.encode("ascii")
        if len(name) >= 24:
            raise SystemExit(f"pack file name too long: {pack.name}")
        data = pack.read_bytes()
        code = data[8:16].split(b"\0", 1)[0].decode("ascii", "replace")
        if data[:4] != b"KSLP" or code != pack.stem:
            raise SystemExit(f"{pack.name} is not the language pack of {pack.stem!r}")
        entries += name.ljust(24, b"\0") + struct.pack("<II", start + len(body), len(data))
        body += data
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"KSLB" + struct.pack("<I", len(packs)) + entries + body)
    print(f"bundle: {len(packs)} packs, {(8 + len(entries) + len(body)) // 1024} KB -> {output}")


def main():
    global MINIMUM_WORDS
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--languages", nargs="*", help="codes from LANGUAGES (default: all)")
    parser.add_argument("--out", type=Path, help="folder for the .kslang files")
    parser.add_argument("--code", help="a language outside LANGUAGES (with --word-list)")
    parser.add_argument("--word-list", type=Path, help="UTF-8 word list, most frequent first")
    parser.add_argument("--english-name")
    parser.add_argument("--native-name")
    parser.add_argument("--langid", type=lambda v: int(v, 0), action="append", default=[])
    parser.add_argument("--flags", type=lambda v: int(v, 0), default=CASED)
    parser.add_argument("--bundle", type=Path, help="folder of packs to bundle")
    parser.add_argument("--bundle-out", type=Path)
    parser.add_argument("--minimum-words", type=int, default=None, help=argparse.SUPPRESS)
    parser.add_argument("--if-available", action="store_true",
                        help="skip quietly when wordfreq is not installed")
    args = parser.parse_args()
    if args.minimum_words is not None:
        MINIMUM_WORDS = args.minimum_words

    if args.bundle:
        write_bundle(args.bundle, args.bundle_out or args.bundle / "languages.bundle")
        return
    if not args.out:
        parser.error("--out is required")
    args.out.mkdir(parents=True, exist_ok=True)

    if args.languages is not None and not args.languages:
        parser.error("--languages needs at least one code")
    if args.code:
        if not (args.word_list and args.english_name and args.native_name and args.langid):
            parser.error("--code needs --word-list, --english-name, --native-name and --langid")
        check_code(args.code)
        check_langids(args.langid)
        padded_utf8(args.english_name, 48)
        padded_utf8(args.native_name, 48)
        source = args.word_list.read_text(encoding="utf-8-sig").splitlines()
        data = build_pack(args.code, args.english_name, args.native_name, args.langid, args.flags, source)
        (args.out / f"{args.code}.kslang").write_bytes(data)
        return

    if args.if_available:
        try:
            from importlib import metadata
            import wordfreq  # noqa: F401  (a broken install counts as missing)
            version = metadata.version("wordfreq")
        except Exception:
            print("wordfreq is not installed: no language packs built (pip install wordfreq==3.1.1)")
            return
        if version != "3.1.1":
            print(f"wordfreq {version} is installed, the packs need 3.1.1: no language packs built")
            return
    for code in args.languages or sorted(LANGUAGES):
        if code not in LANGUAGES:
            raise SystemExit(f"unknown language {code}; known: {' '.join(sorted(LANGUAGES))}")
        english_name, native_name, langids, flags = LANGUAGES[code]
        data = build_pack(code, english_name, native_name, langids, flags, wordfreq_source(code))
        (args.out / f"{code}.kslang").write_bytes(data)


if __name__ == "__main__":
    main()
