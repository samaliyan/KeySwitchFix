# Spelling correction

KeySwitchFix corrects spelling offline for Persian and English on top of the
layout repair (since 2.9; rebuilt in 4.1; context in 4.2). It is a
**noisy-channel model**, the formulation production spell checkers use, with a
word-pair model of context, reduced to what fits in a native tray application
with no runtime and no network.

## When it runs

1. The user finishes a word with Space, Enter, Tab, a period, or Shift+Space
   (ZWNJ). Spelling is never evaluated mid-word: an unfinished word and a
   misspelled one are indistinguishable, and the layout repair already covers
   the live case.
2. The layout logic runs first. If the word's other-layout reading is a known
   word (a layout mistake or a collision), spelling correction is not
   consulted.
3. English is limited to all-lowercase words (a capital letter marks a name,
   an acronym, or code; a capital that the sentence-capitalisation helper
   itself added does not count, so `Teh` at a sentence start still becomes
   `The`); Persian is limited to letter-only tokens (no digits, ZWNJ, or
   diacritics).
4. Words the user has shown to be theirs are skipped: a word undone with
   Backspace, a word the model left alone twice this session, a word kept in
   the personal dictionary, and a word the writing memory has seen often.

## The lexicon: which words are real

A word is known when it is in any of these:

- the **rank table** (`resources/en-rank.bin`, `fa-rank.bin`): up to 65,536
  frequent words per language with their frequency, generated from wordfreq
  3.1.1 by `tools/generate_rank_tables.py`;
- the **dictionary Bloom** (`en.bloom`, `fa.bloom`): the Hunspell stems;
- the **word-form Bloom** (`en-forms.bloom`, `fa-forms.bloom`, 4.1): every
  inflected form the Hunspell affix rules allow — 121,202 English and
  1,247,798 Persian forms (`hordes`, `certifies`, `مخالفم`, `کتابهایشان`).
  Hunspell keeps inflections as rules, so before 4.1 a valid inflected word
  missing from the frequency table looked like a typo and could be
  "corrected" into its neighbour (`hordes` → `horses`, `اسیران` → `ایران`).
  Built by `tools/generate_form_blooms.py` from the same dictionary packages
  as the other Blooms (false positives ~0.03 % English, ~0.16 % Persian).

The rank table's acceptance rule decides what counts as a real word, since
being too strict rewrites valid words and being too loose keeps corpus typos.
Words are examined most-frequent first and accepted when any of these holds:

1. it is in the dictionary or word-form Bloom (or the short-word lists). A
   token that is two everyday words run together (`alot`, `درخانه`) is
   rejected first, so that the missing-space repair can fire: only when it is
   below zipf 4.0 **and** at least 2.5 zipf (300×) rarer than both halves —
   closed compounds (`backend`, `signup`) must stay whole, and a Persian word
   plus a colloquial ending is one word (`بازیا` is `بازی` + `ا`, not
   `باز یا`; `زندگیتو` is not `زندگی تو`);
2. it has zipf ≥ 4.0 — everyday vocabulary even when a formal dictionary
   omits it (`thx`, `pls`, `میخوام`, `چطوری`);
3. no accepted word within one edit is at least 10× more frequent. A rare
   token next to a very frequent word (`teh`/`the`, `recieve`/`receive`) is a
   corpus misspelling. **Persian counts only a spelling-type neighbour**
   (4.1): a letter of the same sound or shape (`برگذار`/`برگزار`,
   `خوشکل`/`خوشگل`), a missing madda (`اتش`/`آتش`), or a vowel letter or hamza
   dropped inside the word (`تلوزیون`/`تلویزیون`). Colloquial Persian differs
   from the written form by its *endings* — `نمیدونه`/`نمیدونم`, `خونت`/`خونه`,
   `قدیما`/`قدیمی`, `شدین`/`شدن`, `نیار`/`نیاز` — and those are words. Before
   4.1 they were treated as typos and could be changed, sometimes into a word
   of another meaning;
4. it is dominated only by its own stem(s) (`kayaks`, `گوشیش`) — while
   `runing` (dominated by `running`) is not.

The Persian table now holds 26,353 words (23,164 in 4.0). The tables are a
build product listed in `.gitignore`; `build-native.sh` generates them, which
requires `pip install wordfreq==3.1.1`.

## Candidates

**One slip.** Every string within Damerau–Levenshtein distance one of the
typed word: one letter transposed, one letter too many, one wrong letter, one
letter missing. Only candidates in the rank table count.

**Two slips** (4.1, Balanced and Aggressive, words of 4–12 letters). When no
single repair is convincing, two slips are tried, each from a short list of
likely ones: a neighbouring key or a Persian homophone for the right letter,
two letters swapped, a doubled or neighbouring extra letter, a letter not
doubled, and — for one of the two — a missing vowel letter or a missing key
next to its neighbours. `یغتی` → `یعنی` (two neighbouring keys). A two-slip
repair never removes the first or last letter by a detour (`webs` is not
`web`), and only words exactly two edits away count (a one-letter repair is
never re-scored by a two-step detour). The search takes up to about 5 ms for a
12-letter word that nothing explains, against the hook's 150 ms budget.

**A rare word that is a slip** (4.1, Balanced and Aggressive, 4+ letters). A
word that is real but only *weakly* known — a dictionary form missing from the
frequency table, or a word the writing memory has seen fewer than eight times
— yields to a very frequent word (zipf ≥ 4.5) one strong slip away (a
neighbouring key, a homophone, swapped or doubled letters) when that word
clearly beats leaving it as typed: `خرفه` (purslane) → `حرفه` (profession),
`هشتیم` → `هستیم`, `خروشید` → `خورشید`. Removing the first or last letter is
never such a slip (`نخرید` is not `خرید`). A word in the frequency table, a
vocabulary-pack term, and a word the user kept are never touched this way.

## Scoring

```
score(candidate) = 10 · zipf(candidate) + error_model(typed → candidate)
```

| Slip | Bonus | Example |
| --- | --- | --- |
| Two letters swapped | +24 | `teh` → `the`, `نسهخ` → `نسخه` |
| Wrong letter: a Persian homophone or shape twin (ح/ه، ت/ط، س/ص/ث، ز/ذ/ض/ظ، ق/غ، ا/آ/أ/إ/ع، ی/ئ، و/ؤ، ک/گ) | +30 | `صلام` → `سلام`, `خوشکل` → `خوشگل` |
| Wrong letter on a neighbouring key | +24 | `wprd` → `word`, `خانع` → `خانه` |
| Wrong letter elsewhere | +6 | |
| Extra letter that doubles its neighbour | +22 | `helllo` → `hello` |
| Extra letter from a neighbouring key (also the first or last letter, 4.1) | +18 | `طرفل` → `طرف`, `عهمگی` → `همگی` |
| Extra letter elsewhere | +8 | |
| Extra first or last letter that is not a neighbouring key | Aggressive only, +0 | |
| Missing letter that would double its neighbour | +24 | `adress` → `address` |
| Missing vowel letter (ا و ی ه, a e i o u) | +14 | |
| Missing letter elsewhere | +12 | `wrd` → `word` |
| Two slips | the two bonuses − 28 | `یغتی` → `یعنی` |

An extra first or last letter that is a neighbouring key of the letter next to
it is a slip, not an ending: valid endings are in the word-form Bloom and never
reach the search. Keyboard neighbours are computed on the physical QWERTY
grid; Persian letters map to the same keys.

## Joins

- **Missing half-space (ZWNJ).** `میپرسیدند` → `می‌پرسیدند` when the word is
  not itself an everyday word and the stem after `می`/`نمی` looks like a verb
  form; nouns that start with `می` (`میهمان`, `میدان`) stay whole. Suffix
  joins (`کتابها` → `کتاب‌ها`) are Aggressive-only; the possessive endings
  (`مان`, `شان`, …) join only a word no dictionary form explains, so
  `کتابهایشان` is never split as `کتابهای‌شان`.
- **Missing space.** `inthe`, `alot`, `درخانه` split into two very common words.

## Context (4.2)

A typo is often ambiguous on its own (`مثلل`: `مثل` or `مثلا`?) and obvious in
its sentence. Two sources say which words go together:

- **Persian word pairs** (`resources/fa-pairs.bin`, 4 MB): 734,003 ordered
  pairs of words from 79 million words of Persian Wikipedia, each with how
  much more often the second word follows the first than chance would
  predict (10 × pointwise mutual information, 0–60). Pairs no more frequent
  than chance are left out: a missing pair means "no evidence", never "wrong".
- **Your own pairs** (writing memory on): two words you typed one after the
  other at least twice weigh 20, rising to 40 with use. Written Persian does
  not know colloquial phrases; your own writing does.

A candidate gains its pair score with the word before it and with the word
after it (at most 40 in all), so the repair that fits the sentence wins, and
an ambiguous typo becomes clear. Leaving a rare word as typed gains its own
context score the same way, so a rare word that fits its neighbours stays.

**When the next word decides.** Spelling runs when a word ends, before the
next word exists. A word it could not decide alone (no convincing repair, or
a rare word that may be a slip) is marked; when the next word is finished
(with Space), the marked word is looked at again with both neighbours. If it
is now clear, the screen goes from `به بزان مادری` to `به زبان مادری`: the
previous word is repaired in place, the way layout repair rewrites earlier
words of a phrase. One Backspace right after restores both words as typed and
keeps the previous one as yours. This never happens in Word, Outlook or
OneNote (they rewrite text behind the caret themselves), after a click or
a Tab (the history is gone), across a full stop, or for the second part of
a word joined with a half-space. A long sentence keeps its context: the word
history drops its oldest words (earlier versions dropped all of them after
31 words).

Measured on 40,000 typos placed in sentences of Wikipedia articles that were
held out of the counts (Balanced, Persian):

| Context | Fixed | Wrong word | Correct words changed |
| --- | --- | --- | --- |
| none (4.1) | 68 % | 4.5 % | 6 of 30,000 |
| the word before | 79 % | 4.1 % | 8 of 30,000 |
| both neighbours | 84 % | 4.0 % | 7 of 30,000 |

The test sentences are written Persian like the counts, so everyday
colloquial writing gains less from the corpus pairs; your own pairs make up
the difference over time. English has no corpus pair table yet; your own
English pairs work.

## Decision

The best candidate wins only if all of these hold for the chosen level:

| Level | Minimum zipf of winner | Margin over runner-up | Two slips | Rare-word slips |
| --- | --- | --- | --- | --- |
| Conservative | 4.0 | 20 | no | no |
| Balanced (default) | 3.0 (two slips: 4.0) | 12 (two slips: 18) | yes | yes |
| Aggressive | 2.2 (two slips: 4.0) | 6 (two slips: 12) | yes | yes |

Three-letter words need a winner with error-model support (Conservative never
touches them). For a rare-word slip the runner-up is at least the score of
leaving the word as typed (60). The margin keeps ambiguous words untouched:
`ther` sits between `there` and `their`; `مثلل` between `مثل` (a doubled
letter) and `مثلا` (a neighbouring key). For such words the writing memory is
the answer: repair `مثلل` to `مثلا` by hand twice and it is learned.

## Measured quality

`tests/fixtures/spell-eval.tsv` (built by `tools/make_spell_eval.py`) holds
4,000 typos generated from the 4,000 most frequent words of each language with
keyboard slips, and 5,300 valid words (frequent corpus words and rare
dictionary stems). `tests/spell_real_tests.c` runs the engine over it with the
real tables at every build and fails when a change makes spelling worse.

Balanced, typos generated from words the tuning never saw:

| | Fixed | Wrong word | Valid words changed |
| --- | --- | --- | --- |
| Persian 4.0 | 54 % | 6.5 % | 2.9 % of corpus words |
| Persian 4.1 | 64 % | 5.3 % | 0.4 % of corpus words, 0.6 % of rare dictionary words |
| English 4.0 | 61 % | 3.9 % | 3.7 % of corpus words |
| English 4.1 | 82 % | 2.9 % | 2.2 % of corpus words |

"Fixed" counts only the exact intended word. Many of the remaining misses are
genuinely ambiguous (`پاکح`: `پاک` or `پاکه`?) and are left alone on purpose;
most "valid words changed" in English are lowercase names (`nye` → `bye`).
Generated typos are not real typing, so the numbers compare versions rather
than predict a user's experience.

## Where it does not run

Spelling correction is skipped below Aggressive in code editors and terminals
(Windows Terminal, cmd, PowerShell, VS Code, Visual Studio, JetBrains IDEs,
Sublime, Notepad++, SSMS, SQL Developer, DBeaver, Git Bash, WSL, …), where
identifiers such as `bool` or `endl` sit one edit away from a frequent word.
Layout repair works there. **Excluded apps** disables everything.

## Your own spellings: Undo, keep, repair

- **One Backspace right after a correction** (or `Ctrl + Win + Backspace`)
  restores what you typed. The word is then yours for the rest of the session.
- **Remember undone words** (dashboard, on by default since 4.1): the restored
  word is saved for good once you type on — anything but Backspace or Delete,
  or after ten seconds. If you keep deleting the restored word instead, that
  Backspace was not a choice and nothing is saved. A word restored twice in
  one session is saved at once. The words are in
  `%LOCALAPPDATA%\KeySwitchFix\personal-dictionary.txt`; tray menu
  **Writing memory → Open my kept words…** opens it; delete a line and save,
  and that word is correctable again within two seconds (a word you undid in
  this session stays yours until the program restarts).
- **Repair by hand** (writing memory on): fixing a word yourself — delete the
  last letters and retype, or Space, Backspace back into the word, and fix —
  teaches the repair (applied automatically from the second time) and, since
  4.1, makes the writing memory forget the typo as one of your words, however
  often it slipped through before.
- **Words you type often** (writing memory on) are yours: a word typed eight
  times or more is never corrected. A word seen three to seven times only
  protects itself from guesses, not from a confident fix to a very frequent
  word one slip away — so a typo that slipped through a few times does not
  become permanent.

## Settings

- Dashboard: **Spelling** — Off / Conservative / Balanced / Aggressive.
- Tray menu: **Fix spelling mistakes** toggles between Off and the last level.
- Dashboard: **Remember undone words** — the personal dictionary.
- `settings.ini`: `[Spelling] Level=0..3`, `LastLevel=1..3`,
  `PersonalDictionary=0|1` (default 1 since 4.1; a settings file from an
  earlier version keeps its value).

## Rebuilding the data

```
pip install wordfreq==3.1.1
npm install --prefix /tmp/kd dictionary-en@4.0.0 dictionary-fa@2.0.0
python3 tools/generate_form_blooms.py --dictionary-root /tmp/kd/node_modules
python3 tools/generate_rank_tables.py
python3 tools/make_spell_eval.py --dictionary-root /tmp/kd/node_modules
```

The word-pair table comes from the Persian Wikipedia dump of 2018-10-01 in
JSON form (github.com/Text-Mining/Persian-Wikipedia-Corpus, commit
b3e157ce7fcc, five zip files of 370 MB; CC BY-SA 3.0):

```
python3 tools/count_word_pairs.py --out pairs-fa.npz CORPUS/*.zip
python3 tools/count_word_pairs.py --holdout --out holdout-fa.txt CORPUS/*.zip
python3 tools/generate_pair_table.py pairs-fa.npz --out resources/fa-pairs.bin --slots 1048576
python3 tools/make_context_eval.py --holdout holdout-fa.txt --dictionary-root /tmp/kd/node_modules
```

Articles whose Id is divisible by 20 are never counted: they are the test
sentences. The form Blooms, the pair table and both evaluation sets are
committed; the rank tables are generated by every build. The tuning constants are at the top of
`src/spell.c`; each can be overridden at compile time to measure a change
against the evaluation set.

## Known limits

- Context is one word on each side, from written Persian and your own
  phrases; grammar beyond that (`their`/`there`) is not modelled, and a word
  still ambiguous in its sentence is left alone.
- The corpus pairs are Persian Wikipedia (2018): colloquial phrases and new
  words are missing there until your own writing teaches them.
- The rare-word rule changes about 0.6 % of rare Persian dictionary words when
  they are typed on purpose (`خزید` → `خرید`); one Backspace keeps such a word.
- English uses the en_US dictionary: British spellings (`behaviours`,
  `fibre`) that are not frequent enough for the table may be changed at
  Balanced.
- Three or more slips in one word are not attempted.
- Persian words typed with a ZWNJ are evaluated as their halves.
