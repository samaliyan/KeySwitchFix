#!/usr/bin/env python3
"""Build tests/fixtures/spell-eval.tsv: the fixed spelling evaluation set.

Typos are generated from the 4,000 most frequent words (3-12 letters) of each
language with the slips people make on a physical keyboard: a neighbouring
key, a Persian homophone, two swapped letters, a missing inner letter, a
doubled letter, an extra neighbouring key, two neighbouring keys, and two
mixed slips. Valid words are corpus words (zipf >= 2.5) and dictionary stems
missing from the corpus: the spelling engine must leave them alone. A slip
that lands on any valid form (an inflection too) is not used as a typo.
tests/spell_real_tests.c runs the engine over the set with the real tables
and fails when a change makes spelling worse (docs/SPELLING.md).

    python3 tools/make_spell_eval.py --dictionary-root DIR

Needs wordfreq 3.1.1 and the dictionary-en 4.0.0 / dictionary-fa 2.0.0 npm
packages under DIR. Deterministic: the same inputs give the same file.
"""
import argparse
from pathlib import Path
from importlib import metadata

ROOT = Path(__file__).resolve().parents[1]

import random, sys, unicodedata, re
R = random.Random(20261010)
DICTIONARIES = None

def norm_fa(w):
    w = w.replace('ي', 'ی').replace('ى', 'ی').replace('ك', 'ک').replace('‌', '')
    return ''.join(c for c in unicodedata.normalize('NFC', w) if unicodedata.category(c) != 'Mn')

FA_ALPHA = set('ابپتثجچحخدذرزژسشصضطظعغفقکگلمنوهیآئؤأإء')
EN_ROWS = ['qwertyuiop[]', "asdfghjkl;'", 'zxcvbnm,./']
FA_KEYS = {'ش':'a','ذ':'b','ز':'c','ی':'d','ث':'e','ب':'f','ل':'g','ا':'h','ه':'i','ت':'j','ن':'k',
           'م':'l','پ':'m','د':'n','خ':'o','ح':'p','ض':'q','ق':'r','س':'s','ف':'t','ع':'u','ر':'v',
           'ص':'w','ط':'x','غ':'y','ظ':'z','ج':'[','چ':']','ک':';','گ':"'",'و':','}
KEY_FA = {v: k for k, v in FA_KEYS.items()}
CONF = ['حه', 'تط', 'سصث', 'زذضظ', 'قغ', 'اآع', 'یئ']

def pos(k):
    for r, row in enumerate(EN_ROWS):
        if k in row: return r, row.index(k)
def neighbours_key(k):
    p = pos(k)
    if not p: return []
    r, c = p; out = []
    for rr, cc in [(r, c-1), (r, c+1), (r-1, c), (r-1, c+1), (r+1, c), (r+1, c-1)]:
        if 0 <= rr < 3 and 0 <= cc < len(EN_ROWS[rr]): out.append(EN_ROWS[rr][cc])
    return out
def neighbours(ch, lang):
    if lang == 'en':
        return [k for k in neighbours_key(ch) if k.isalpha()]
    k = FA_KEYS.get(ch)
    if not k: return []
    return [KEY_FA[n] for n in neighbours_key(k) if n in KEY_FA]
def homophones(ch):
    for s in CONF:
        if ch in s: return [c for c in s if c != ch]
    return []

def sub_adj(w, lang):
    i = R.randrange(len(w)); n = neighbours(w[i], lang)
    return w[:i] + R.choice(n) + w[i+1:] if n else None
def sub_homo(w, lang):
    idx = [i for i, c in enumerate(w) if homophones(c)]
    if lang != 'fa' or not idx: return None
    i = R.choice(idx); return w[:i] + R.choice(homophones(w[i])) + w[i+1:]
def transpose(w, lang):
    idx = [i for i in range(len(w)-1) if w[i] != w[i+1]]
    if not idx: return None
    i = R.choice(idx); return w[:i] + w[i+1] + w[i] + w[i+2:]
def delete(w, lang):
    if len(w) < 4: return None
    i = R.randrange(1, len(w)-1)  # inner letter
    return w[:i] + w[i+1:]
def double(w, lang):
    i = R.randrange(len(w)); return w[:i+1] + w[i] + w[i+1:]
def insert_adj(w, lang):
    i = R.randrange(len(w)); n = neighbours(w[i], lang)
    if not n: return None
    return w[:i+1] + R.choice(n) + w[i+1:] if R.random() < .5 else w[:i] + R.choice(n) + w[i:]
def two_adj(w, lang):
    if len(w) < 4: return None
    i, j = sorted(R.sample(range(len(w)), 2)); a = neighbours(w[i], lang); b = neighbours(w[j], lang)
    if not a or not b: return None
    return w[:i] + R.choice(a) + w[i+1:j] + R.choice(b) + w[j+1:]
def two_mixed(w, lang):
    if len(w) < 5: return None
    f1 = R.choice([sub_adj, transpose, double, delete] + ([sub_homo] if lang == 'fa' else []))
    f2 = R.choice([sub_adj, transpose, double] + ([sub_homo] if lang == 'fa' else []))
    a = f1(w, lang)
    return f2(a, lang) if a else None
CLASSES = [('adjacent', sub_adj), ('homophone', sub_homo), ('transpose', transpose), ('delete', delete),
           ('double', double), ('insert', insert_adj), ('two-adjacent', two_adj), ('two-mixed', two_mixed)]

def load(lang):
    from wordfreq import iter_wordlist, zipf_frequency
    f = {w: zipf_frequency(w, lang) for w in iter_wordlist(lang)}
    out = {}
    for w, z in f.items():
        if lang == 'fa':
            if '‌' in w: continue
            w = norm_fa(w)
            if not w or not all(c in FA_ALPHA for c in w): continue
        else:
            w = w.lower()
            if not re.fullmatch('[a-z]+', w): continue
        out[w] = max(out.get(w, 0), z)
    return out
def dic(lang):
    words = set()
    for line in open(DICTIONARIES / f'dictionary-{lang}' / 'index.dic', encoding='utf-8-sig').read().splitlines()[1:]:
        w = line.split('/', 1)[0]
        w = norm_fa(w) if lang == 'fa' else w.lower()
        if lang == 'fa' and w and all(c in FA_ALPHA for c in w): words.add(w)
        if lang == 'en' and re.fullmatch('[a-z]+', w): words.add(w)
    return words

def forms(lang):
    sys.path.insert(0, str(ROOT / "tools"))
    import generate_form_blooms as fb
    package = DICTIONARIES / f"dictionary-{lang}"
    return fb.persian_forms(package) if lang == "fa" else fb.english_forms(package)


def main():
    global DICTIONARIES
    parser = argparse.ArgumentParser()
    parser.add_argument("--dictionary-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "tests" / "fixtures" / "spell-eval.tsv")
    arguments = parser.parse_args()
    if metadata.version("wordfreq") != "3.1.1":
        raise SystemExit("wordfreq: expected 3.1.1")
    DICTIONARIES = arguments.dictionary_root
    typos, valid = [], []
    for lang in ('fa', 'en'):
        freq = load(lang)
        top = [w for w, z in sorted(freq.items(), key=lambda kv: -kv[1]) if 3 <= len(w) <= 12][:4000]
        # Every valid form (inflections too) is a word: a slip that lands on
        # one is a real-word error, not a typo to score.
        all_words = set(freq) | dic(lang) | forms(lang)
        for w in top:
            for name, fn in CLASSES:
                typo = fn(w, lang)
                if not typo or typo == w: continue
                kind = 'realword' if typo in all_words else name
                typos.append((lang, typo, w, kind))
        corpus = [w for w, z in freq.items() if z >= 2.5 and len(w) >= 3]
        stems = sorted(w for w in dic(lang) if len(w) >= 3)
        valid += [(lang, w, 'corpus') for w in corpus]
        valid += [(lang, w, 'dictionary') for w in stems if w not in freq]
    pick = random.Random(41)
    rows = []
    for lang in ('fa', 'en'):
        t = [x for x in typos if x[0] == lang and x[3] != 'realword']
        rows += [('T', lang, x[1], x[2], x[3]) for x in pick.sample(t, 2000)]
        for source, count in (('corpus', 1500), ('dictionary', 1500 if lang == 'fa' else 800)):
            v = [x for x in valid if x[0] == lang and x[2] == source]
            rows += [('V', lang, x[1], '', source) for x in pick.sample(v, count)]
    with open(arguments.out, 'w', encoding='utf-8') as out:
        out.write('# kind\tlang\ttyped\tintended\tclass — generated by tools/make_spell_eval.py (seed 41)\n')
        for row in rows:
            out.write('\t'.join(row) + '\n')
    print(f"{len(rows)} rows written to {arguments.out}")


if __name__ == "__main__":
    main()
