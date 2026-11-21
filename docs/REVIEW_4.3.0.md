# Review of 4.3.0

A full independent review of the program, started from a user report:
typing English, words such as `github` or `gemini` were sometimes deleted
and replaced by another word.

## Method

1. **Measurement first.** The hook simulation (`tests/app_sim.c`) was driven
   with whole word lists instead of hand-picked cases: the 80,000 most
   frequent English tokens and the 27,000 Persian ones of wordfreq, each
   typed correctly (how many are changed?) and on the wrong layout (how
   many are repaired?).
2. **Three independent reviewers** read the program in parallel: the hook
   and the word model; the dashboard, files, clipboard, Setup and the
   build; the core, the typing helpers, the writing memory and the tools.
   Each confirmed its findings by experiment where it could.
3. **Fixes with tests.** Every behaviour change has a check in the corpus
   test, the spelling test with the real tables, or the hook simulation.

## Measured

| Words typed through the hook, Balanced | 4.2.0 | 4.3.0 |
| --- | --- | --- |
| 80,000 English words typed correctly: changed | 4,937 (6.2 %) | 454 (0.6 %) |
| of those, by spelling | 4,346 | 143 (nearly all real misspellings) |
| of those, converted to Persian | 591 | 311 (three-letter tokens that are common Persian words) |
| 27,143 Persian words typed correctly: changed | 198 | 164 (mostly wanted: `اورده` → `آورده`, `دریک` → `در یک`) |
| 20,000 English words on the Persian layout: not repaired | 156 | 121 |
| 20,000 Persian words on the English layout: not repaired | 159 | 163 |
| Evaluation set, English valid corpus words changed (Balanced / Aggressive) | 2.60 % / 5.13 % | 0 % / 0 % |
| Evaluation set, English typos fixed (Balanced) | 82.0 % | 82.0 % |

## Findings and fixes

| Finding | Severity | Fix |
| --- | --- | --- |
| A token in no dictionary was a typo by definition: names, brands, British spellings were rewritten (`zach` → `each`, `binance` → `finance`, `practise` → `practice`). | major | The wide lexicon: corpus-attested tokens are kept; corpus misspellings are filtered out when it is built. |
| The beginning of a word still being typed was converted when the other reading was a word (`قشره` → `ravi`, `kav` → `نشر`), and the rest of the word followed in the wrong alphabet. | major | Beginnings of wide tokens and a known word followed by more letters wait for the end of the word; a rare target waits for a pause. |
| Persian words ending in و ک گ counted as an English word plus punctuation, even as a frequent one. | major | The punctuation core of the target counts only for a finished token that is nothing in the layout in use. |
| Punctuation on letter keys broke the word next to it («…», `[…]`, `12, and`, `sci-fi`). | major | Brackets and guillemets are stripped for the lookup; a letterless token ends the phrase history; a fragment after a hyphen or digit is not judged alone. |
| A frequent word of the other layout replaced a common word of the layout in use on frequency alone. | major | A reduced frequency tier when the typed word is common; none for two keys. |
| The uninstaller's self-delete command could not run; the program folder stayed. | major | `/S` and an outer pair of quotes. |
| After 512 learned repairs every new one evicted the one before it. | major | Eviction rotates among the weakest. |
| A failed read of the writing memory or the snippets looked like an empty file. | major | A failed read returns nothing; the file is kept. |
| Setup closed with Esc after installing did not start the new version. | minor | Close means Finish once the installation is complete. |
| A cancelled clean-up left the selection on the clipboard; metafile-only content was emptied. | minor | The landed copy is adopted before the restore; uncopyable content cancels the clean-up. |
| Clean-up changed letters in URLs and file names and operators of code in Persian text. | minor | Machine tokens keep their letters; symbol tokens after Latin text are left alone. |
| A layout chosen by hand was no evidence for itself. | minor | A manual switch counts as mild context for the next words. |
| Media keys were read as letters. | minor | They are passed through untouched. |
| Punctuation after Undo was shaped for the language just taken back. | minor | Undo restores the word's language for the helpers. |
| The tray icon could be destroyed although it was a shared icon. | minor | Ownership is recorded only when the icon was loaded as owned. |

## Known and not changed

- With no context, a three-letter token that is a common Persian word is
  still taken as Persian (`sms` → `سپس`, `lil` → `مهم`): that is the
  program's purpose for a Persian writer. Inside English text, after a
  manual switch, or once the writing memory knows the word, it stays.
- A token the corpus does not attest and no list holds (a very new or very
  rare name) can still be repaired to a dictionary word one slip away.
  Backspace restores it and keeps it; `tools/lexicon/en-words.txt` is the
  place for words many users need.
- The frequent-word Blooms and the collision priors are older than the
  current generator (a reviewer's finding); regenerating them needs the
  pinned Hunspell dictionaries and a new evaluation, and was left for a
  later release. The two-key frequency rule above removes the reported
  effect.
- `settings.ini` is written in the ANSI code page: an excluded program whose
  name has letters outside it is not kept across a restart.
- Clipboard snapshots and settings writes run on the thread that owns the
  hooks; a hung clipboard owner can delay keys for the hook timeout.
- The release workflow builds with a write token in scope; splitting it
  into a read-only build job and a publish job is recommended.

## Not verified here

- The Windows build: the Zig compiler is not available in the review
  environment, so GitHub Actions builds and checks the executables.
- Real applications and real keyboards: everything above was measured in
  the hook simulation.
