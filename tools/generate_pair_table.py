#!/usr/bin/env python3
"""Build the word-pair table (resources/fa-pairs.bin) from counted pairs.

Input: the .npz written by tools/count_word_pairs.py. For every ordered pair
of words (a, b) seen at least --min-count times, the table stores how much
more often b follows a than chance would predict — the pointwise mutual
information, log10(count(a b) * N / (count(a) * count(b))), times 10, rounded
and clamped to 0..60. Pairs that are not more frequent than chance are left
out: a missing pair simply means "no evidence".

The table uses the KSRT format of the rank tables (tools/generate_rank_tables.py,
ks_rank_lookup in src/spell.c): the key is the text "a b" (one space), so the
program looks a pair up exactly as it looks up a word.

    python3 tools/generate_pair_table.py pairs-fa.npz --out resources/fa-pairs.bin
"""

import argparse
import math
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from generate_rank_tables import build_rank_table  # noqa: E402

MAX_SCORE = 60


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pairs")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--min-count", type=int, default=3)
    parser.add_argument("--min-score", type=int, default=3,
                        help="leave out pairs below this score (10 x PMI)")
    parser.add_argument("--slots", type=int, default=1 << 21)
    parser.add_argument("--max-pairs", type=int, default=0,
                        help="keep at most this many pairs, most frequent first (0: fill to 70 %%)")
    arguments = parser.parse_args()

    data = np.load(arguments.pairs, allow_pickle=True)
    words, unigram = data["words"], data["unigram"]
    first, second, count = data["first"], data["second"], data["count"]
    total = float(data["tokens"])
    keep = count >= arguments.min_count
    first, second, count = first[keep], second[keep], count[keep]
    pmi = np.log10(count * total / (unigram[first].astype(np.float64) * unigram[second]))
    score = np.clip(np.rint(pmi * 10), 0, MAX_SCORE).astype(np.int64)
    keep = score >= arguments.min_score
    first, second, count, score = first[keep], second[keep], count[keep], score[keep]
    order = np.argsort(-count, kind="stable")
    limit = arguments.max_pairs or int(arguments.slots * 0.7)
    order = order[:limit]
    # build_rank_table stores round(zipf * 10): pass score / 10 to keep it.
    entries = ((words[first[i]] + " " + words[second[i]], score[i] / 10.0) for i in order)
    data_bytes, stored, dropped = build_rank_table(entries, arguments.slots)
    arguments.out.write_bytes(data_bytes)
    print(f"{arguments.out.name}: {stored} pairs, {len(data_bytes)} bytes, {len(dropped)} dropped "
          f"(of {int(keep.sum())} pairs with count >= {arguments.min_count} and score >= {arguments.min_score})")


if __name__ == "__main__":
    main()
