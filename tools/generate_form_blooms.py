#!/usr/bin/env python3
"""Build the word-form Blooms: every inflected form the Hunspell dictionaries
allow (kayaks, hiked, certifies; کتاب‌هایشان, مخالفم), in the same lookup form
as the base dictionary Blooms.

The base Blooms (resources/en.bloom, fa.bloom) hold only dictionary stems:
Hunspell keeps inflections as affix rules. Spelling correction needs the
forms too, or a valid inflected word that is missing from the frequency
table looks like a typo and is "corrected" into a neighbouring word
(hordes -> horses, اسیران -> ایران).

    python3 tools/generate_form_blooms.py --dictionary-root DIR

DIR holds the dictionary-en 4.0.0 and dictionary-fa 2.0.0 npm packages (the
same input as tools/generate_blooms.py). The outputs are committed.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import generate_blooms as blooms  # noqa: E402

FORM_HASH_COUNT = 9
ENGLISH_FORM_BITS = 1 << 21   # ~121,000 forms: false positives ~0.03 %
PERSIAN_FORM_BITS = 1 << 24   # ~1,248,000 forms: false positives ~0.16 %
ENGLISH_PATTERN = re.compile(r"[a-z]+(?:'[a-z]+)?")


# ---- A Hunspell affix expander ---------------------------------------------
#
# Enough of Hunspell for these two dictionaries: prefixes and suffixes with
# strip/add/condition, one level of suffix continuation (Persian ان + م), and
# the cross product of a cross-product prefix with a cross-product suffix.
# Compounding flags are ignored: compounds are not words on their own.

def parse_affixes(path: Path):
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    flag_mode = "char"
    rules = {}
    index = 0
    while index < len(lines):
        parts = lines[index].split()
        if len(parts) >= 2 and parts[0] == "FLAG":
            flag_mode = parts[1]
        if len(parts) == 4 and parts[0] in ("PFX", "SFX") and parts[2] in ("Y", "N"):
            kind, flag, cross, count = parts[0], parts[1], parts[2] == "Y", int(parts[3])
            entries = []
            for offset in range(count):
                fields = lines[index + 1 + offset].split()
                strip = "" if fields[2] == "0" else fields[2]
                add, _, continuation = fields[3].partition("/")
                add = "" if add == "0" else add
                condition = fields[4] if len(fields) > 4 else "."
                pattern = None
                if condition != ".":
                    pattern = re.compile(("^" + condition) if kind == "PFX" else (condition + "$"))
                entries.append((strip, add, continuation, pattern))
            rules[flag] = (kind, cross, entries)
            index += count
        index += 1
    return flag_mode, rules


def split_flags(text: str, mode: str):
    if mode == "long":
        return [text[i:i + 2] for i in range(0, len(text), 2)]
    if mode == "num":
        return [flag for flag in text.split(",") if flag]
    return list(text)


def apply_rule(word: str, rule):
    kind, _, entries = rule
    for strip, add, continuation, pattern in entries:
        if pattern and not pattern.search(word):
            continue
        if kind == "SFX":
            if strip and not word.endswith(strip):
                continue
            yield (word[: -len(strip)] if strip else word) + add, continuation
        else:
            if strip and not word.startswith(strip):
                continue
            yield add + (word[len(strip):] if strip else word), continuation


def expand(package: Path):
    flag_mode, rules = parse_affixes(package / "index.aff")
    forms = set()
    for line in blooms.dictionary_lines(package):
        word, _, flags = line.partition("/")
        word = word.strip()
        if not word:
            continue
        flag_text = flags.split()[0] if flags.split() else ""
        own = [flag for flag in split_flags(flag_text, flag_mode) if flag in rules]
        forms.add(word)
        suffixed = []
        for flag in own:
            if rules[flag][0] != "SFX":
                continue
            for form, continuation in apply_rule(word, rules[flag]):
                forms.add(form)
                suffixed.append((form, flag))
                for next_flag in split_flags(continuation, flag_mode):
                    if next_flag in rules and rules[next_flag][0] == "SFX":
                        for second, _ in apply_rule(form, rules[next_flag]):
                            forms.add(second)
        for flag in own:
            if rules[flag][0] != "PFX":
                continue
            for form, _ in apply_rule(word, rules[flag]):
                forms.add(form)
            if rules[flag][1]:
                for form, suffix_flag in suffixed:
                    if rules[suffix_flag][1]:
                        for both, _ in apply_rule(form, rules[flag]):
                            forms.add(both)
    return forms


def english_forms(package: Path):
    result = set()
    for form in expand(package):
        word = form.lower()
        if len(word) >= 2 and ENGLISH_PATTERN.fullmatch(word):
            result.add(word)
    return result


def persian_forms(package: Path):
    result = set()
    for form in expand(package):
        word = blooms.normalize_persian(form)
        if len(word) >= 2 and word.isalpha():
            result.add(word)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dictionary-root", type=Path, required=True,
                        help="Directory containing dictionary-en and dictionary-fa npm packages")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "resources")
    arguments = parser.parse_args()

    english_package = arguments.dictionary_root / "dictionary-en"
    persian_package = arguments.dictionary_root / "dictionary-fa"
    blooms.require_version(english_package, "4.0.0")
    blooms.require_version(persian_package, "2.0.0")

    english = english_forms(english_package)
    persian = persian_forms(persian_package)
    # Every stem the base Bloom knows is a form too.
    english |= blooms.english_words(english_package)
    persian |= blooms.persian_words(persian_package)
    (arguments.output_dir / "en-forms.bloom").write_bytes(
        blooms.build_bloom(sorted(english), ENGLISH_FORM_BITS, FORM_HASH_COUNT))
    (arguments.output_dir / "fa-forms.bloom").write_bytes(
        blooms.build_bloom(sorted(persian), PERSIAN_FORM_BITS, FORM_HASH_COUNT))
    print(f"Word forms: {len(english)} English, {len(persian)} Persian.")


if __name__ == "__main__":
    main()
