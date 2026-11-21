#!/usr/bin/env python3
"""Build the English word-pair table (resources/en-pairs.bin).

The Persian table is counted from Wikipedia (tools/count_word_pairs.py).
For English the counts come ready-made with SymSpell: 242,342 word pairs
and 82,765 word counts derived from the Google Books Ngram corpus, shipped
in the symspellpy package (MIT; the Ngram data is CC BY 3.0, see
third-party/en-pairs-NOTICE.txt).

As in tools/generate_pair_table.py, a pair's score is how much more often
the second word follows the first than chance predicts: 10 x log10 of
count(a b) * N / (count(a) * count(b)), clamped to 0..60. Pairs that are
not more frequent than chance are left out. The table is a KSRT table
keyed by "a b" (ks_spell_pair_score in src/spell.c).

    pip install symspellpy==6.10.2
    python3 tools/generate_en_pair_table.py
"""

import argparse
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from generate_rank_tables import build_rank_table  # noqa: E402

MAX_SCORE = 60
MIN_SCORE = 3
# The pair counts and the word counts of the source are on different
# scales (the pairs are a filtered subset with scaled counts), which lifts
# every score by a constant. "of the" is about five times likelier than
# chance (score 7) and comes out as 25: subtract the difference.
CALIBRATION = 18
SLOTS = 1 << 18
WORD = re.compile(r"[a-z]+")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=ROOT / "resources" / "en-pairs.bin")
    arguments = parser.parse_args()
    import symspellpy

    folder = Path(symspellpy.__file__).resolve().parent
    unigram = {}
    for line in (folder / "frequency_dictionary_en_82_765.txt").read_text(encoding="utf-8").splitlines():
        word, count = line.split()
        unigram[word] = int(count)
    total = float(sum(unigram.values()))
    pairs = []
    for line in (folder / "frequency_bigramdictionary_en_243_342.txt").read_text(encoding="utf-8").splitlines():
        first, second, count = line.split()
        if not WORD.fullmatch(first) or not WORD.fullmatch(second):
            continue
        if first not in unigram or second not in unigram:
            continue
        score = round(10 * math.log10(int(count) * total / (unigram[first] * unigram[second]))) - CALIBRATION
        score = max(0, min(MAX_SCORE, score))
        if score >= MIN_SCORE:
            pairs.append((int(count), first + " " + second, score))
    pairs.sort(key=lambda pair: (-pair[0], pair[1]))
    entries = ((key, score / 10.0) for _, key, score in pairs)
    data, stored, dropped = build_rank_table(entries, SLOTS)
    arguments.out.write_bytes(data)
    print(f"{arguments.out.name}: {stored} pairs, {len(data)} bytes, {len(dropped)} dropped")


if __name__ == "__main__":
    main()
