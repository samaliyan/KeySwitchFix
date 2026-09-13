#!/usr/bin/env python3
"""Build the KSRT frequency-rank tables used by spelling correction.

A KSRT table is an open-addressing hash table with 4-byte slots: a 24-bit
fingerprint of the word plus one byte holding round(zipf * 10). The layout,
hash, and probe sequence are identical to ks_rank_lookup() in src/spell.c.

Real tables (resources/en-rank.bin, resources/fa-rank.bin) come from
wordfreq 3.1.1. The table is the spelling lexicon, so its acceptance rule
decides what counts as a "real word"; being too strict rewrites valid words,
being too loose keeps typos. A frequent word is accepted when any of these
holds, in order:

1. it is in the base dictionary Bloom (or the short-word lists); a token that
   is two everyday words run together (alot, thankyou, درخانه) is rejected
   before any other rule so that the split repair can fire;
2. it is common enough (zipf >= 4.0) to be everyday vocabulary even when a
   formal dictionary omits it (thx, lol, pls, میخوام, چطوری);
3. no already-accepted word within edit distance one is at least 1.0 zipf
   (10x) more frequent. A rare token that sits next to a very frequent word
   (teh/the, recieve/receive) is a misspelling in the corpus and must stay
   out; a rare token with no dominant neighbour is simply a rare word;
4. it is dominated only by its own stem(s): the Hunspell resources store
   stems, so kayaks, hiked, spreadsheets, گوشیش, کتابهایم are recovered by
   stripping up to two common suffixes — while runing (dominated by running)
   or ther (dominated by there/their) are not.

    python3 tools/generate_rank_tables.py            # both languages
    python3 tools/generate_rank_tables.py --if-missing
    python3 tools/generate_rank_tables.py --fixture words.txt out.bin

A fixture file holds one "word zipf" pair per line and needs no wordfreq.
"""

import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

SLOT_COUNT = 1 << 17
WORD_LIMIT = 65536
MINIMUM_WORDS = 16384
CASUAL_ZIPF_FLOOR = 4.0
MINIMUM_LENGTH = 1
MAXIMUM_LENGTH = 32


def hash_pair(value: str):
    first = 2166136261
    second = 5381
    for byte in value.encode("utf-8"):
        first = ((first ^ byte) * 16777619) & 0xFFFFFFFF
        second = (((second << 5) + second) ^ byte) & 0xFFFFFFFF
    return first, second


def rank_hash(value: str):
    first, second = hash_pair(value)
    fingerprint = first & 0xFFFFFF
    if fingerprint == 0:
        fingerprint = 1
    index_hash = ((second * 0x9E3779B9) & 0xFFFFFFFF) ^ (first >> 7)
    return fingerprint, index_hash


def build_rank_table(entries, slot_count=SLOT_COUNT):
    """entries: iterable of (word, zipf); earlier entries win collisions."""
    slots = [0] * slot_count
    mask = slot_count - 1
    stored = 0
    dropped = []
    for word, zipf in entries:
        rank = max(0, min(255, int(round(zipf * 10))))
        fingerprint, index = rank_hash(word)
        index &= mask
        placed = False
        for _ in range(128):
            value = slots[index]
            if value == 0:
                slots[index] = fingerprint | (rank << 24)
                stored += 1
                placed = True
                break
            if value & 0xFFFFFF == fingerprint:
                # Same fingerprint inside one probe run: a lookup could not
                # tell the two words apart. Keep the more frequent (earlier).
                dropped.append(word)
                placed = True
                break
            index = (index + 1) & mask
        if not placed:
            dropped.append(word)
    header = b"KSRT" + struct.pack("<III", 1, slot_count, stored)
    body = struct.pack("<%dI" % slot_count, *slots)
    return header + body, stored, dropped


def bloom_contains(data: bytes, word: str) -> bool:
    _, _, bit_count, hash_count = struct.unpack("<4sIII", data[:16])
    mask = bit_count - 1
    first, second = hash_pair(word)
    second = ((second << 1) | 1) & 0xFFFFFFFF
    bits = data[16:]
    for index in range(hash_count):
        bit = (first + index * second + index * index * 0x9E3779B9) & mask
        if not bits[bit >> 3] & (1 << (bit & 7)):
            return False
    return True


ENGLISH_SUFFIXES = ("s", "es", "ed", "d", "ing", "er", "ers", "ly", "est", "n", "ness")
PERSIAN_SUFFIXES = (
    "ها", "های", "هایی", "هایم", "هایت", "هایش", "تر", "ترین",
    "م", "ت", "ش", "مان", "تان", "شان", "ی", "ای", "ان", "ات", "گان",
)
# A token is treated as a corpus misspelling when an accepted word within one
# edit is at least this much more frequent (1.0 zipf = 10x). Real misspellings
# usually run at 1-5 % of the correct form.
DOMINANT_NEIGHBOUR_GAP = 1.0


def known_stems(word: str, language: str, known) -> set:
    """Known dictionary stems reachable by stripping one or two suffixes."""
    suffixes = ENGLISH_SUFFIXES if language == "en" else PERSIAN_SUFFIXES
    found = set()
    frontier = {word}
    for _ in range(2):
        next_frontier = set()
        for stem in frontier:
            for suffix in suffixes:
                if not stem.endswith(suffix) or len(stem) - len(suffix) < 3:
                    continue
                base = stem[: -len(suffix)]
                if known(base):
                    found.add(base)
                # kayak/kayaks; hike/hiked and hiking (dropped e)
                if language == "en" and suffix in ("d", "ed", "ing", "er", "ers") and known(base + "e"):
                    found.add(base + "e")
                next_frontier.add(base)
        frontier = next_frontier
    return found


def distance_one_neighbours(word: str, alphabet: str):
    length = len(word)
    for i in range(length - 1):
        if word[i] != word[i + 1]:
            yield word[:i] + word[i + 1] + word[i] + word[i + 2:]
    for i in range(length):
        yield word[:i] + word[i + 1:]
    for i in range(length):
        for letter in alphabet:
            if letter != word[i]:
                yield word[:i] + letter + word[i + 1:]
    for i in range(length + 1):
        for letter in alphabet:
            yield word[:i] + letter + word[i:]


def dominant_neighbours(word: str, zipf: float, accepted: dict, alphabet: str) -> set:
    """Accepted words within one edit that are far more frequent than word."""
    found = set()
    for neighbour in distance_one_neighbours(word, alphabet):
        other = accepted.get(neighbour)
        if other is not None and other - zipf >= DOMINANT_NEIGHBOUR_GAP:
            found.add(neighbour)
    return found


JOINED_PAIR_GAP = 2.5


def joined_pair(word: str, zipf: float, accepted: dict) -> bool:
    """alot, ofthe, thankyou, درخانه: two everyday words missing their space.

    Closed compounds (backend, signup, hotspot) are routinely 1.5-2.5 zipf
    below their halves, so this must be conservative: the token has to be
    below the casual floor AND at least JOINED_PAIR_GAP (300x) rarer than
    both halves. A missed joined typo costs one un-repaired "alot"; a false
    positive here rewrites a real word at every level.
    """
    if zipf >= CASUAL_ZIPF_FLOOR:
        return False
    for split in range(1, len(word) - 1):
        left, right = word[:split], word[split:]
        left_floor = 6.0 if split == 1 else CASUAL_ZIPF_FLOOR
        left_zipf = accepted.get(left, 0.0)
        right_zipf = accepted.get(right, 0.0)
        if left_zipf >= left_floor and right_zipf >= CASUAL_ZIPF_FLOOR and \
                zipf <= min(left_zipf, right_zipf) - JOINED_PAIR_GAP:
            return True
    return False


def accept_word(word: str, zipf: float, language: str, known, accepted: dict, alphabet: str) -> bool:
    if known(word):
        return True
    # Corpus tokens that are really two words (alot, thankyou) are frequent
    # but must not become "words", or the split repair could never fire.
    if joined_pair(word, zipf, accepted):
        return False
    if zipf >= CASUAL_ZIPF_FLOOR:
        return True
    dominators = dominant_neighbours(word, zipf, accepted, alphabet)
    if not dominators:
        return True
    # An inflection is rescued only when everything that dominates it is its
    # own stem (webs <- web, کتابهایم <- کتاب). A dominated token whose
    # dominator is *not* its stem (runing <- running, ther <- there/their,
    # thes <- these) is a misspelling and must stay out of the lexicon.
    return dominators <= known_stems(word, language, known)


def read_fixture(path: Path):
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        word, zipf = line.split()
        yield word, float(zipf)


def real_entries(language: str):
    from wordfreq import iter_wordlist, zipf_frequency

    import generate_blooms as blooms

    blooms.validate_wordfreq()
    resources = ROOT / "resources"
    base = (resources / f"{language}.bloom").read_bytes()
    english_pattern = re.compile(r"[a-z]+")
    short_words = (
        blooms.SHORT_ENGLISH_WORDS if language == "en" else blooms.SHORT_PERSIAN_WORDS
    )

    alphabet = (
        "abcdefghijklmnopqrstuvwxyz"
        if language == "en"
        else "ابپتثجچحخدذرزژسشصضطظعغفقکگلمنوهیآئؤأإء"
    )

    def known(candidate: str) -> bool:
        # The curated dictionary only. The common Bloom is raw corpus
        # frequency and contains joined typos (alot, thankyou).
        return candidate in short_words or bloom_contains(base, candidate)

    # Pass 1: well-formed tokens with their frequency, most frequent first.
    shaped = []
    seen = set()
    for raw_word in iter_wordlist(language):
        if language == "en":
            word = raw_word.lower()
            if not english_pattern.fullmatch(word):
                continue
        else:
            if "\u200c" in raw_word:
                continue
            word = blooms.normalize_persian(raw_word)
            if not word.isalpha() or word in blooms.PERSIAN_COMMON_REJECT:
                continue
        if not MINIMUM_LENGTH <= len(word) <= MAXIMUM_LENGTH or word in seen:
            continue
        # Single letters only when they are words (a, I, و): they anchor the
        # one-letter left half of a split (alot -> a lot) and nothing else.
        if len(word) == 1 and word not in short_words:
            continue
        zipf = zipf_frequency(word, language)
        if zipf <= 0.0:
            continue
        seen.add(word)
        shaped.append((word, zipf))
        if len(shaped) == WORD_LIMIT * 3:
            break
    shaped.sort(key=lambda entry: -entry[1])

    # Pass 2: acceptance in frequency order, so every neighbour that could
    # dominate a word has already been decided when the word is examined.
    result = []
    accepted = {}
    for word, zipf in shaped:
        if not accept_word(word, zipf, language, known, accepted, alphabet):
            continue
        accepted[word] = zipf
        result.append((word, zipf))
        if len(result) == WORD_LIMIT:
            break
    if len(result) < MINIMUM_WORDS:
        raise SystemExit(f"wordfreq {language}: only {len(result)} eligible words")
    return result


def write_table(path: Path, entries, label: str, slot_count=SLOT_COUNT, strict=False):
    data, stored, dropped = build_rank_table(entries, slot_count)
    if dropped and strict:
        raise SystemExit(
            f"{label}: {len(dropped)} words could not be stored without a "
            f"fingerprint collision ({', '.join(dropped[:5])}); enlarge the table"
        )
    path.write_bytes(data)
    print(f"{label}: {stored} words, {len(data)} bytes, {len(dropped)} dropped")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=ROOT / "resources")
    parser.add_argument("--if-missing", action="store_true",
                        help="do nothing when both tables already exist")
    parser.add_argument("--fixture", nargs=2, metavar=("WORDLIST", "OUTPUT"),
                        help="build a table from a 'word zipf' text file")
    parser.add_argument("--slots", type=int, default=SLOT_COUNT,
                        help="slot count for --fixture (power of two, >= 256)")
    arguments = parser.parse_args()

    if arguments.fixture:
        wordlist, output = map(Path, arguments.fixture)
        write_table(output, list(read_fixture(wordlist)), output.name, arguments.slots)
        return

    outputs = {
        "en": arguments.output_dir / "en-rank.bin",
        "fa": arguments.output_dir / "fa-rank.bin",
    }
    if arguments.if_missing and all(path.exists() for path in outputs.values()):
        print("Rank tables already present.")
        return
    for language, path in outputs.items():
        write_table(path, real_entries(language), path.name, strict=True)


if __name__ == "__main__":
    main()
