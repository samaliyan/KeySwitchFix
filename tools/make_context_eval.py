#!/usr/bin/env python3
"""Build tests/fixtures/spell-context-eval.tsv: spelling in context.

From Persian Wikipedia articles that were held out of the pair counts
(tools/count_word_pairs.py --holdout, articles whose Id is divisible by 20),
one word per sentence, with the word before and after it:

    T  before  typo  after  intended  class   a keyboard slip of the word
    V  before  word  after                    the word as written

tests/spell_real_tests.c checks that the context makes spelling better and
that words written correctly stay as they are.

    python3 tools/count_word_pairs.py --holdout --out holdout-fa.txt CORPUS.zip ...
    python3 tools/make_context_eval.py --holdout holdout-fa.txt --dictionary-root DIR

Needs wordfreq 3.1.1 and the dictionary packages (as tools/make_spell_eval.py).
Deterministic for the same inputs.
"""
import argparse
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import make_spell_eval as base  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--holdout", type=Path, required=True)
    parser.add_argument("--dictionary-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "tests" / "fixtures" / "spell-context-eval.tsv")
    parser.add_argument("--typos", type=int, default=4000)
    parser.add_argument("--valid", type=int, default=3000)
    arguments = parser.parse_args()
    base.DICTIONARIES = arguments.dictionary_root
    rng = random.Random(4242)
    base.R = rng
    freq = base.load("fa")
    every_word = set(freq) | base.dic("fa") | base.forms("fa")
    lines = arguments.holdout.read_text(encoding="utf-8").split("\n")
    rng.shuffle(lines)
    rows = []
    typos = valid = 0
    for line in lines:
        words = line.split()
        if len(words) < 3:
            continue
        i = rng.randrange(1, len(words) - 1)
        before, word, after = words[i - 1], words[i], words[i + 1]
        if not 3 <= len(word) <= 12 or word not in freq:
            continue
        if valid < arguments.valid:
            rows.append(("V", before, word, after, "", "valid"))
            valid += 1
        name, make = rng.choice(base.CLASSES)
        typo = make(word, "fa")
        if typos < arguments.typos and typo and typo != word and typo not in every_word:
            rows.append(("T", before, typo, after, word, name))
            typos += 1
        if typos >= arguments.typos and valid >= arguments.valid:
            break
    with open(arguments.out, "w", encoding="utf-8") as out:
        out.write("# kind\tbefore\ttyped\tafter\tintended\tclass — tools/make_context_eval.py (seed 4242)\n")
        for row in rows:
            out.write("\t".join(row) + "\n")
    print(f"{typos} typos and {valid} valid words in context written to {arguments.out}")


if __name__ == "__main__":
    main()
