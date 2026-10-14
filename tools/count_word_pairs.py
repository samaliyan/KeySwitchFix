#!/usr/bin/env python3
"""Count word pairs (bigrams) in a Persian text corpus.

Input: the Persian Wikipedia JSON dump of github.com/Text-Mining/
Persian-Wikipedia-Corpus (fawiki-20181001, one JSON object per line with a
"Text" field, zipped; CC BY-SA 3.0), or plain UTF-8 text files.

Words are split the way KeySwitchFix sees them while you type: a run of
Persian letters; a space or a half-space (ZWNJ) ends a word, and anything
else (digits, Latin letters, punctuation) ends the sentence, so no pair
spans it. Letters are normalised as the dictionaries are (Arabic ي ك become
ی ک, diacritics are dropped).

Articles whose Id is divisible by 20 are held out (--holdout) for the
evaluation: they are counted into nothing.

Output (numpy .npz): the vocabulary, the unigram counts, and every pair seen
at least twice as (first id, second id, count).

    python3 tools/count_word_pairs.py --out pairs-fa.npz CORPUS.zip ...
"""

import argparse
import io
import json
import re
import sys
import unicodedata
import zipfile
from pathlib import Path

import numpy as np

PERSIAN_LETTERS = "ابپتثجچحخدذرزژسشصضطظعغفقکگلمنوهیآئؤأإء"
LETTER = set(PERSIAN_LETTERS)
# Tokens: Persian letter runs, or single separators we must see.
TOKEN = re.compile("[" + PERSIAN_LETTERS + "]+|[ ‌\t]+|[^" + PERSIAN_LETTERS + " ‌\t]+")


def normalise(text: str) -> str:
    text = text.replace("ي", "ی").replace("ى", "ی").replace("ك", "ک").replace("ۀ", "ه").replace("ة", "ه")
    text = unicodedata.normalize("NFC", text)
    return "".join(c for c in text if unicodedata.category(c) != "Mn")


def articles(paths, holdout):
    for path in paths:
        path = Path(path)
        if path.suffix == ".zip":
            with zipfile.ZipFile(path) as archive:
                for name in archive.namelist():
                    with archive.open(name) as raw:
                        for line in io.TextIOWrapper(raw, encoding="utf-8"):
                            try:
                                record = json.loads(line)
                            except ValueError:
                                continue
                            if record.get("Namespace", 0) != 0:
                                continue
                            held = int(record.get("Id", 0)) % 20 == 0
                            if held == holdout:
                                yield record.get("Text") or ""
        elif not holdout:
            yield path.read_text(encoding="utf-8")


def sentences(text: str):
    """Lists of words that follow each other with only spaces between."""
    current = []
    for token in TOKEN.findall(normalise(text)):
        first = token[0]
        if first in LETTER:
            current.append(token)
        elif first in " ‌\t":
            continue
        else:
            if len(current) > 1:
                yield current
            current = []
    if len(current) > 1:
        yield current


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("corpus", nargs="+")
    parser.add_argument("--out", required=True)
    parser.add_argument("--holdout", action="store_true",
                        help="write the held-out sentences (Id % 20 == 0) as text instead")
    arguments = parser.parse_args()

    if arguments.holdout:
        with open(arguments.out, "w", encoding="utf-8") as out:
            for text in articles(arguments.corpus, True):
                for words in sentences(text):
                    out.write(" ".join(words) + "\n")
        return

    vocabulary = {}
    unigram = []
    chunk = []
    merged_keys = np.zeros(0, dtype=np.int64)
    merged_counts = np.zeros(0, dtype=np.int64)
    tokens = 0

    def flush():
        nonlocal merged_keys, merged_counts, chunk
        if not chunk:
            return
        keys, counts = np.unique(np.asarray(chunk, dtype=np.int64), return_counts=True)
        chunk = []
        all_keys = np.concatenate([merged_keys, keys])
        all_counts = np.concatenate([merged_counts, counts])
        order = np.argsort(all_keys, kind="stable")
        all_keys, all_counts = all_keys[order], all_counts[order]
        boundaries = np.flatnonzero(np.diff(all_keys)) + 1
        starts = np.concatenate([[0], boundaries])
        merged_keys = all_keys[starts]
        merged_counts = np.add.reduceat(all_counts, starts)

    for number, text in enumerate(articles(arguments.corpus, False), 1):
        for words in sentences(text):
            ids = []
            for word in words:
                index = vocabulary.get(word)
                if index is None:
                    index = len(unigram)
                    vocabulary[word] = index
                    unigram.append(0)
                unigram[index] += 1
                ids.append(index)
            tokens += len(ids)
            chunk.extend((ids[i] << 24) | ids[i + 1] for i in range(len(ids) - 1))
        if len(chunk) > 20_000_000:
            flush()
        if number % 50000 == 0:
            print(f"{number} articles, {tokens} words, {len(merged_keys)} pairs so far", file=sys.stderr, flush=True)
    flush()
    if len(unigram) >= 1 << 24:
        raise SystemExit("vocabulary too large for 24-bit ids")
    keep = merged_counts >= 2
    words = np.array(sorted(vocabulary, key=vocabulary.get), dtype=object)
    np.savez_compressed(arguments.out, words=words, unigram=np.array(unigram, dtype=np.int64),
                        first=(merged_keys[keep] >> 24).astype(np.int32),
                        second=(merged_keys[keep] & 0xFFFFFF).astype(np.int32),
                        count=merged_counts[keep].astype(np.int64), tokens=np.int64(tokens))
    print(f"{tokens} words, {len(unigram)} distinct, {int(keep.sum())} pairs seen twice or more")


if __name__ == "__main__":
    main()
