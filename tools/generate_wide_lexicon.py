#!/usr/bin/env python3
"""Build the wide lexicons: every token a large corpus attests as a word.

The curated dictionaries (resources/*.bloom) and the spelling rank tables
hold the words a dictionary lists. People also type names, brands, places,
abbreviations, British spellings and new words (zach, binance, kaggle,
practise). Without them a correctly typed name looks like a typo of the
nearest dictionary word (zach -> each) or like the other keyboard layout
(sms -> سپس). The wide lexicon is the answer: a Bloom of every token that
wordfreq's large English list attests at zipf >= 1.5 (about one use in 30
million words), the Persian list, and the curated lists in tools/lexicon/,
minus the tokens that are known misspellings.

A corpus also attests misspellings (recieve, teh, definately). A token is
left out, so spelling still repairs it, when it is not a dictionary word
and a dictionary word one edit away is far more frequent *and* the edit is
the kind people make by mistake rather than the kind that separates two
names (see likely_misspelling), or when tools/lexicon/en-misspellings.txt
lists it (the classic ones, and everyday words run together: alot, ofthe).
Run-together words cannot be told from names by rule: hasan is "has an"
and asher "as her" exactly as ofthe is "of the".

Outputs (resources/, build products like the rank tables):

    en-wide.bloom         attested English tokens
    en-wide-prefix.bloom  their proper prefixes of three letters or more
    fa-wide.bloom         attested Persian tokens and the curated list
    fa-wide-prefix.bloom  their proper prefixes

    python3 tools/generate_wide_lexicon.py
    python3 tools/generate_wide_lexicon.py --if-missing
"""

import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

ENGLISH_FLOOR = 1.5
WORD_HASHES = 9
PREFIX_HASHES = 6
MINIMUM_PREFIX = 3
MAXIMUM_LENGTH = 32
VOWELS = set("aeiouy")
# A dictionary word must be this much more frequent (zipf: 1.8 = 63x) than
# the token for the token to be its misspelling.
MISSPELLING_GAP = 1.8


def hash_pair(value: str):
    first = 2166136261
    second = 5381
    for byte in value.encode("utf-8"):
        first = ((first ^ byte) * 16777619) & 0xFFFFFFFF
        second = (((second << 5) + second) ^ byte) & 0xFFFFFFFF
    return first, second


def build_bloom(words, hash_count: int, bits_per_word: int, minimum_bits: int = 1 << 16) -> bytes:
    """A KSWB Bloom (the format of ks_bloom_init) sized for the word count."""
    words = sorted(set(words))
    bit_count = minimum_bits
    while bit_count < len(words) * bits_per_word:
        bit_count <<= 1
    mask = bit_count - 1
    bits = bytearray(bit_count >> 3)
    for word in words:
        first, second = hash_pair(word)
        second = ((second << 1) | 1) & 0xFFFFFFFF
        for index in range(hash_count):
            bit = (first + index * second + index * index * 0x9E3779B9) & mask
            bits[bit >> 3] |= 1 << (bit & 7)
    return b"KSWB" + struct.pack("<III", 1, bit_count, hash_count) + bytes(bits)


def prefixes_of(words):
    found = set()
    for word in words:
        for length in range(MINIMUM_PREFIX, len(word)):
            found.add(word[:length])
    return found


def read_list(name: str):
    path = ROOT / "tools" / "lexicon" / name
    if not path.exists():
        return []
    words = []
    for line in path.read_text(encoding="utf-8").splitlines():
        words.extend(line.split("#", 1)[0].split())
    return words


def edit_kind(word: str, other: str):
    """How `word` differs from `other`, one edit apart: 'swap', 'double'
    (a doubled letter typed once, or a letter doubled by mistake), 'vowel'
    (one vowel for another), 'inner' (a letter missing or added inside the
    word), 'edge' (at its first or last position), 'letter' (any other
    letter changed)."""
    if len(word) == len(other):
        differ = [i for i in range(len(word)) if word[i] != other[i]]
        if len(differ) == 2 and differ[1] == differ[0] + 1 and \
                word[differ[0]] == other[differ[1]] and word[differ[1]] == other[differ[0]]:
            return "swap"
        if len(differ) == 1:
            i = differ[0]
            if word[i] in VOWELS and other[i] in VOWELS:
                return "vowel"
            return "edge" if i in (0, len(word) - 1) else "letter"
        return None
    shorter, longer = (word, other) if len(word) < len(other) else (other, word)
    if len(longer) - len(shorter) != 1:
        return None
    for i in range(len(longer)):
        if longer[:i] + longer[i + 1:] == shorter:
            if (i > 0 and longer[i - 1] == longer[i]) or (i + 1 < len(longer) and longer[i + 1] == longer[i]):
                return "double"
            return "edge" if i in (0, len(longer) - 1) else "inner"
    return None


def likely_misspelling(word: str, zipf: float, accepted: dict, neighbours) -> bool:
    """A corpus token that is a misspelling of a dictionary word rather
    than a word (a name, a brand, a variant spelling) of its own.

    Frequency alone cannot tell: `zach` is as much rarer than `each` as
    `recieve` is than `receive`. The kind of edit and the length can. Two
    swapped letters, a doubled letter typed once, a changed vowel or a
    letter dropped inside a long word are how words are misspelt
    (recieve, commited, definately, goverment). One different letter, or
    any edit in a short token, is how names differ from words
    (levin/kevin, tsar/star, bleu/blue), and a token that is a word plus
    or minus its last letter is an ending or a foreign form (materia,
    livin). Changing a correctly typed name is worse than leaving a typo,
    so the rules are strict; the classic short misspellings are listed in
    tools/lexicon/en-misspellings.txt instead.
    """
    length = len(word)
    for neighbour in neighbours(word):
        other = accepted.get(neighbour)
        if other is None or other - zipf < MISSPELLING_GAP:
            continue
        kind = edit_kind(word, neighbour)
        if kind == "swap":
            # teh, adn, thier: a short swap only against an everyday word.
            if length >= 6 or (length >= 4 and other >= 5.5 and other - zipf >= 2.5):
                return True
        elif kind == "double":
            # commited, begining; an added double only in a long word
            # (carefull): willson, bonnett and novell are names.
            if length >= 8 or (length == 7 and len(neighbour) > length):
                return True
        elif kind in ("vowel", "inner") and length >= 8:
            return True
    return False


def english_words():
    from wordfreq import get_frequency_dict, zipf_frequency

    import generate_blooms as blooms
    import generate_rank_tables as ranks

    blooms.validate_wordfreq()
    base = (ROOT / "resources" / "en.bloom").read_bytes()
    forms = (ROOT / "resources" / "en-forms.bloom").read_bytes()
    alphabet = "abcdefghijklmnopqrstuvwxyz"
    accepted = dict(ranks.real_entries("en"))
    pattern = re.compile(r"[a-z]{3,%d}" % MAXIMUM_LENGTH)
    never = set(read_list("en-misspellings.txt"))
    always = set(word.lower() for word in read_list("en-words.txt"))

    def known(candidate: str) -> bool:
        return ranks.bloom_contains(base, candidate) or ranks.bloom_contains(forms, candidate)

    def neighbours(word: str):
        return ranks.distance_one_neighbours(word, alphabet)

    kept = set()
    dropped = []
    for token in get_frequency_dict("en", "large"):
        if not pattern.fullmatch(token) or token in never:
            continue
        zipf = zipf_frequency(token, "en", "large")
        if zipf < ENGLISH_FLOOR:
            continue
        if token not in accepted and not known(token) and token not in always:
            if likely_misspelling(token, zipf, accepted, neighbours):
                dropped.append(token)
                continue
        kept.add(token)
    for word in always:
        if pattern.fullmatch(word):
            kept.add(word)
    kept -= never
    return kept, dropped


def persian_words():
    from wordfreq import iter_wordlist

    import generate_blooms as blooms
    import generate_rank_tables as ranks

    accepted = dict(ranks.real_entries("fa"))
    kept = set(word for word in accepted if len(word) >= 3)
    for word in read_list("fa-words.txt"):
        word = blooms.normalize_persian(word.replace("‌", ""))
        if len(word) >= 3:
            kept.add(word)
    return kept


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=ROOT / "resources")
    parser.add_argument("--if-missing", action="store_true")
    parser.add_argument("--report", type=Path, help="write the tokens left out as misspellings here")
    arguments = parser.parse_args()
    names = ["en-wide.bloom", "en-wide-prefix.bloom", "fa-wide.bloom", "fa-wide-prefix.bloom"]
    if arguments.if_missing and all((arguments.output_dir / name).exists() for name in names):
        print("Wide lexicons already present.")
        return
    english, dropped = english_words()
    persian = persian_words()
    outputs = {
        "en-wide.bloom": build_bloom(english, WORD_HASHES, 14),
        "en-wide-prefix.bloom": build_bloom(prefixes_of(english), PREFIX_HASHES, 9),
        "fa-wide.bloom": build_bloom(persian, WORD_HASHES, 14),
        "fa-wide-prefix.bloom": build_bloom(prefixes_of(persian), PREFIX_HASHES, 9),
    }
    for name, data in outputs.items():
        (arguments.output_dir / name).write_bytes(data)
        print(f"{name}: {len(data)} bytes")
    print(f"English: {len(english)} tokens, {len(dropped)} left out as misspellings; Persian: {len(persian)} tokens")
    if arguments.report:
        arguments.report.write_text("\n".join(dropped) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
