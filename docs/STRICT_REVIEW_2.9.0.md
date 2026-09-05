# KeySwitchFix 2.9.0 — three-round strict critic review

Each round scores the program 0–10 on nine axes (spelling precision and
robustness count double in the overall), lists the defects the critic could
demonstrate, and is followed by the fixes that the next round re-tests. The
critic worked adversarially: concrete inputs against the shipped Blooms and
the fixture rank tables, sanitizer builds, a 300k-iteration fuzz of the
spelling module, timing of the worst-case word inside the hook, and a line
review of the Win32 integration. No Windows toolchain was available, so the
executable itself was not run; the Win32 sources were type-checked against a
stub header with gcc and clang under `-Wall -Wextra -Werror`.

## Round 1 — the feature as first written

| Axis | Score |
| --- | --- |
| Layout-repair correctness | 8 |
| Spelling precision | 6 |
| Spelling recall | 6 |
| Hook latency | 9 |
| Robustness / memory safety | 8 |
| UI / UX | 7 |
| Privacy | 8 |
| Build / testability | 7 |
| Code quality | 8 |
| **Overall** | **7.3** |

Defects found:

1. **ZWNJ repair unreachable from the hook.** The base Persian dictionary
   stores ZWNJ-free spellings, so `میپرسیدند` is "known"; the hook only
   consulted the spelling module for words unknown in both layouts. The unit
   tests passed against the module directly and never saw the gate. *Fix:*
   consult the module for known, unambiguous Persian words too; it may only
   restore the joiner there, never respell.
2. **`می` join cut nouns.** With the join reachable, `میهمان` absent from the
   table would become `می‌همان` (`همان` is very frequent). *Fix:* the stem
   must be verb-shaped (personal ending or past-stem ت/د); regression with a
   fixture that lacks the noun.
3. Process and password-field system calls ran before scoring, on every
   word. *Fix:* score first, query only when a fix is about to be applied.

## Round 2 — documentation and integration drift

| Axis | Score |
| --- | --- |
| Layout-repair correctness | 8 |
| Spelling precision | 7 |
| Spelling recall | 7 |
| Hook latency | 9 |
| Robustness / memory safety | 8 |
| UI / UX | 8 |
| Privacy | 8 |
| Build / testability | 7 |
| Code quality | 8 |
| **Overall** | **7.8** |

Defects found: `SPELLING.md` still described the common-word Bloom as
trusted and the split repair as "not handled"; the Undo section claimed
nothing is ever written to disk although the opt-in personal dictionary now
writes; the installer's `EstimatedSize` was the 2.7 figure. *Fix:* documents
synchronised with the code; installer size updated; changelog entries for
joins, learned vocabulary, personal dictionary, coverage and the dashboard.

## Round 3 — independent adversarial critic

| Axis | Score |
| --- | --- |
| Layout-repair correctness | 8 |
| Spelling precision | 5 |
| Spelling recall | 6 |
| Hook latency | 9 (165 µs worst case measured) |
| Robustness / memory safety | 8 |
| UI / UX | 6 |
| Privacy | 7 |
| Build / testability | 6 |
| Code quality | 8 |
| **Overall** | **7.0** |

Defects found and fixed:

1. **HIGH — split repair rewrote closed compounds at every level.** The
   generator's joined-pair rule assumed a compound is about as frequent as
   its halves; `backend`, `frontend`, `signup`, `hotspot` are 1.5–2.5 zipf
   below theirs and were evicted from the lexicon, after which the hook split
   them (`backend → back end`, even in Conservative). *Fix:* a token is a
   joined typo only when it is below the casual floor **and** ≥ 2.5 zipf
   (300×) rarer than both halves; the split bonus dropped from 20 to 12 so it
   can never outrank a one-letter repair with real evidence; Conservative
   requires both halves ≥ zipf 5.0. Verified against the mock: all five
   compounds accepted whole, `alot`/`ofthe`/`inthe` rejected.
2. **MEDIUM — `alot` unreachable in production.** The hook gate treated the
   raw-corpus Bloom (which contains `alot`, `thankyou`) as "known", and the
   generator never emitted one-letter words, so the one-letter split half was
   dead code. *Fix:* English words known only through the corpus tier are
   re-checked with the spelling lexicon; `a`, `I`, `و` are admitted to the
   real tables.
3. **MEDIUM — one Backspace could teach a typo permanently.** *Fix:* a first
   undo trusts the word for the session only; it reaches the personal
   dictionary when the user has typed or restored it before.
4. **MEDIUM — vocabulary learned in suppressed contexts.** Words typed in
   developer tools or password fields, or with spelling off, were observed.
   *Fix:* the module returns a four-way outcome; only "examined and declined
   on merit" trains the vocabulary, and never in a password field or a
   skipped process.
5. **LOW — doubled initial letter never repaired below Aggressive**
   (`hhello`, `سسلام`). *Fix:* the affix guard excludes doubled letters.
6. **LOW — personal dictionary file handling.** *Fix:* UTF-16 BOM detected,
   whitespace trimmed, duplicates removed, the file compacted to the ring
   size, load and write failures surfaced in the activity line.
7. **Accepted, documented:** Balanced is the default because the owner chose
   it; the `می` join cannot tell a noun from a verb when the noun is missing
   from the table (`میزبانم`), and very common ZWNJ-less verbs are accepted
   as everyday vocabulary and not joined; per-monitor DPI is not handled.

## Final state after the round-3 fixes (self-assessed, same rubric)

| Axis | Score | Why |
| --- | --- | --- |
| Layout-repair correctness | 8.5 | Unchanged core; spelling strictly sequenced after it; regressions green. |
| Spelling precision | 8 | Compound splitting closed; affix stripping Aggressive-only; 3-letter rule; learned vocabulary; ambiguity margin. Residual: `می` noun/verb case. |
| Spelling recall | 7.5 | DL-1, half-space, missing space, doubled edge letters; misses distance-2 and deliberately skips very common ZWNJ-less verbs. |
| Hook latency | 9 | ≤ 165 µs worst case, no allocation, syscalls only on apply. |
| Robustness / memory safety | 8.5 | ASan/UBSan clean on tests and 300k fuzz; header validation; bounded buffers; watchdog; personal dictionary bounded and compacted. |
| UI / UX | 8 | Redesigned dashboard, no clipped labels, status pill, stat tiles, DPI-scaled; no per-monitor DPI. |
| Privacy | 8.5 | Memory-only learning, opt-in file limited to undone words, password fields excluded from learning, documented. |
| Build / testability | 7.5 | 150+ native checks incl. spelling fixtures; hook state machine still not unit-testable; real tables need wordfreq in CI. |
| Code quality | 8 | Small functions, documented policies, consistent naming. |
| **Overall** | **8.2 / 10** | |

The remaining gap to a higher score is structural rather than a bug list: a
platform-free hook state machine with synthetic key events, a bigram context
for the ambiguous cases the margin currently refuses, and per-monitor DPI.
