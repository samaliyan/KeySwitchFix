# Review of 4.1.0

Spelling correction was rebuilt for 4.1 after users reported misspellings it
left alone (`یغتی`, `خرفه ای`, `مثلل`) and asked for a dependable way to keep a
deliberate spelling.

## Method

1. **Exact data, offline.** The wordfreq 3.1.1 data files and the pinned
   Hunspell packages reproduce the 4.0.1 release byte for byte: both rank
   tables and all 14 language packs.
2. **Measurement before change.** Typos were generated from the 4,000 most
   frequent words of each language: neighbouring keys, homophones, swaps,
   missing, doubled and extra letters, and two slips. Valid corpus words and
   rare dictionary stems were added as checks. Every constant was tuned on half
   of the target words and reported on the other half.
3. **Findings that changed the plan.**
   - The largest problem was not the misses. 4.0 changed valid words:
     - Persian conversational endings: `نمیدونه` → `نمیدونم`, `خونت` → `خونه`.
     - Inflections the dictionary keeps only as affix rules: `hordes` → `horses`.
   - The fix was made at the source: the word-form Blooms, and the rank-table
     rule that now treats only spelling-type neighbours as typos.
4. **Independent review.**
   - Three reviewers worked in parallel with no context: the engine, the app
     integration, and the tools/data/tests.
   - A fresh reviewer then checked the fixes.

## Findings and their fixes

| Finding | Severity | Fix |
| --- | --- | --- |
| The rare-word path could delete a first or last letter and flip a Persian negation (`نخرید` → `خرید`) | major | Edge deletions are never used in the rare-word path |
| The two-slip search could re-find a one-letter repair by a two-hop detour, score it higher than its one-slip score, and report it as two slips | major | Two-slip candidates must be exactly two edits away (`within_one_edit`) |
| A newer undo could replace a kept word whose save was still queued | minor | That word is saved first |
| "Kept" was shown when nothing was saved (setting off, failed write) | minor | Saving reports success; the message follows it |
| A word was marked kept in memory before the file write succeeded | minor | Marked only after the write |
| A dictionary file being written by another program could be read as empty | minor | The reload waits until the file can be opened |
| Ctrl+Z after an undo counted as "typing on" | minor | Ctrl/Win shortcuts cancel the keep; AltGr typing does not |
| One hand edit made the memory forget a word the user writes often | minor | Only words seen fewer than eight times are forgotten |
| Colloquial word + ending was split as two words (`بازیا` → `باز یا`, `زندگیتو`) | minor | The joined-word rule skips Persian colloquial endings |
| The evaluation counted inflected valid forms as typos, and did not count splits as changes | minor | Fixed in `make_spell_eval.py` and the test |
| A stale rank table from an older build could be embedded | minor | Both build scripts rebuild the tables when an input is newer |
| Comment figures (Bloom false-positive rate, worst-case time) were wrong | minor | Corrected |

## Mutation checks

Each new behaviour was removed in turn to confirm that a test fails:

- forgetting a hand-repaired typo
- keeping on the next key
- cancelling on Backspace
- two-slip search
- rare-word repair
- saving a queued word before a new undo

## Not verified here

- The Windows build: the Zig cross-compiler download is blocked in the review environment. GitHub Actions builds and checks the executables.
- Behaviour in real applications on Windows.
- How the measured rates translate to real typing: generated typos compare versions, they do not model a person.
