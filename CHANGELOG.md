# Changelog

All notable changes to KeySwitchFix are documented here. The project follows [Semantic Versioning](https://semver.org/).

## [3.0.0] - 2026-09-12

### Added

- Typing helpers, each with its own switch on the dashboard and the tray's
  **Typing helpers** menu; none of them act in code editors, terminals,
  remote-desktop clients, password fields, or excluded apps
  (`docs/TYPING_HELPERS.md`):
  - **Digits** follow the language: `۱۲۳` on the Persian layout, `123` on the
    English layout (or always one form, or off). The numeric keypad is left
    alone.
  - **Persian punctuation**: `? , ;` after a Persian word become `؟ ، ؛`, and
    the reverse after an English word. A manual layout switch clears the
    context so a deliberate `؟` is never overruled.
  - **Persian letters**: the legacy layout's Arabic `ي` and `ك` are typed as
    Persian `ی` and `ک`.
  - **Capitalise English sentences**: the first letter after `.` `!` `?` and
    the lone `i` become capitals, with an abbreviation list (`dr.`, `e.g.`,
    `etc.`, file extensions, months), number and code guards; a capitalised
    word is still spell-checked, so `teh` at a sentence start becomes `The`.
  - **Snippets**: `shortcut = text` lines in `snippets.txt` (UTF-8/UTF-16,
    up to 256 entries, reloaded within two seconds of saving) expand at
    Space/Enter/Tab with Undo on Backspace, and support date/time macros
    including the Jalali calendar: `{jdate}`, `{jdate:en}`, `{jdate:long}`,
    `{jweekday}`, `{date}`, `{date:long}`, `{weekday}`, `{time}`,
    `{time:fa}`, and `\n`/`{t}` for Enter/Tab. **Edit snippets…** creates a
    commented template and opens it.
  - **Clean up selected text** with `Ctrl + Win + X` in any application:
    Arabic letters and digits to Persian, digits per the setting, Persian
    punctuation in Persian text, per word so URLs and e-mail addresses keep
    their digits; the previous clipboard content (all text-like formats) is
    restored afterwards unless something newer was copied.
  - **Statistics** on the dashboard: layout and spelling fixes today, fixes
    all time, keys today, active days, estimated time saved and the
    most-corrected words (memory only). Counters persist in `stats.ini`
    every ten minutes, at exit and at Windows shutdown.
- `src/typing.c` (platform-free, 60+ native checks including a full
  1900–2200 Jalali round trip) and `tests/typing_tests.c`.
- Remote-desktop and virtual-machine clients (`mstsc`, `msrdc`, `vmconnect`,
  VirtualBox, VMware, Citrix, AnyDesk, TeamViewer, RustDesk, Parsec) and
  terminal emulators (`putty`, `kitty`, MobaXterm, SecureCRT) join the list
  of applications where spelling and the helpers stay out of the way.

### Changed

- Dashboard redesigned on a 840×748 grid: a 100-pixel header that keeps the
  name, tagline, status pill and version line inside the gradient; six
  settings rows in three columns; four statistic tiles and a statistics
  line; footer buttons. If the screen's work area is smaller than the scaled
  window (small laptops, high scaling), the whole dashboard scales down so
  the footer is always visible.
- Version 3.0.0; the title bar and header show it.

### Fixed

- `standard` typed on the Persian layout became `staدیشقی` (the first three
  keys were repaired, the rest kept rendering Persian), and the same happened
  to every English word typed on the wrong layout in applications that
  honour the layout switch late or not at all. Root causes: the layout was
  read from, and the switch requested of, the *foreground* window's thread —
  for Store/UWP applications (the Windows 11 Notepad, Settings, Mail,
  WhatsApp, Telegram from the Store, …) that is ApplicationFrameHost.exe, not
  the application, so the switch changed a layout nobody was typing with while
  the hook believed it had succeeded; and a live correction discarded the
  corrected keys, so the remainder of the word was judged on its own.
- The real input thread is now resolved through the focused window or the
  UWP `Windows.UI.Core.CoreWindow`; the switch is verified on that thread and
  re-posted to the other windows of the thread when the focused one swallows
  it; a blocked replacement no longer leaves a pending request behind.
- While a requested switch is pending and keys still arrive in the old layout,
  the hook types the characters of the requested layout itself (letters,
  digits, punctuation, Shift+Space ZWNJ), so the wrong alphabet never reaches
  the screen even if the application never switches; password fields are left
  untouched; a manual switch, click, navigation key, or whole-word deletion
  ends this immediately. The activity line and the diagnostics line name an
  application that did not switch.
- A live correction's word is resumed when the user keeps typing it, so the
  whole word is evaluated (and the spelling model never sees the tail alone).
- Exclusions and the developer-tool list match both the host process and the
  focused control's process (UWP frame, embedded web view); the tray's
  "Exclude this app" names the application rather than the frame host.
- Key-ups of swallowed keys are tracked per key, so fast overlapping typing
  no longer leaks a key-up without its key-down to the application.
- The synchronous wait for the layout switch inside the low-level hook is
  limited to one 50 ms attempt (previously up to 100 ms), staying well inside
  the hook time budget Windows enforces before detaching a hook.
- Shift is read from the physical key state for each key, so a Shift
  transition the hook missed cannot mis-case a translated character.

- The tracked Ctrl/Alt/Win state is cross-checked with the physical key
  state, so a modifier transition the hook missed cannot turn `Ctrl+S` into a
  typed letter.

## [2.9.0] - 2026-09-05

### Added

- Offline spelling correction for Persian and English (`src/spell.c`). At a
  word boundary, a word that is unknown in both layouts is scored against
  every candidate within edit distance one using a noisy-channel model:
  `10 × zipf(candidate)` plus an error model that rewards adjacent-letter
  transpositions, neighbouring keys on the physical keyboard, doubled or
  dropped letters, and Persian homophone confusions (ح/ه، ت/ط، س/ص/ث،
  ز/ذ/ض/ظ، ق/غ، ا/آ/ع). `نسحه` → `نسخه`, `teh` → `the`, `wrold` → `world`.
- Compact `KSRT` frequency-rank tables (256 KB per language, up to 32,768
  words) generated from wordfreq 3.1.1 by `tools/generate_rank_tables.py`;
  casual vocabulary (`thx`, `lol`, `میخوام`) is included so it is recognised,
  not rewritten. Evaluation takes tens of microseconds inside the hook.
- **Spelling** setting (Off / Conservative / Balanced / Aggressive) on the
  dashboard and a **Fix spelling mistakes** toggle on the tray menu. Default:
  Balanced.
- One plain Backspace undoes a spelling fix exactly like a layout fix, and
  teaches a 64-entry in-memory ignore list so the spelling is left alone for
  the rest of the session. Nothing is written to disk.
- `tests/spell_tests.c` with fixture rank tables built by the same generator,
  proving Python/C parity of the table format and covering the decision
  rules (ambiguity, names, acronyms, casual words, ZWNJ, digits, levels).
- Half-space and missing-space repairs: `میپرسیدند` → `می‌پرسیدند` (verb-shaped
  stems only; suffix joins such as `کتاب‌ها` are Aggressive-only), `inthe` →
  `in the`, `alot` → `a lot`, `درخانه` → `در خانه`.
- Learned vocabulary: a word unknown in both layouts that is typed twice in a
  session becomes the user's word and is never corrected afterwards (in
  memory only). Undoing a spelling fix trusts the word immediately.
- Opt-in personal dictionary (**Remember undone words**): undone spellings
  are saved to `personal-dictionary.txt` and trusted across restarts.
- Frequency tables now hold up to 65,536 words per language (512 KB each).
- A redesigned dashboard: header with a live Active/Paused pill, a 200 px
  label column (no more clipped captions such as "Writing langua"), three
  statistics tiles (keys, layout fixes, spelling fixes), DPI-scaled.
- `docs/SPELLING.md` describing the model, thresholds, and limits, and
  `docs/STRICT_REVIEW_2.9.0.md` with the three critic rounds and scores.

### Fixed

- A word split across two layouts (`standard` rendered as `staدیشقی` because
  the target application applied the requested layout switch a few keys late)
  was abandoned at the switch and left as garbage. Each key now remembers the
  layout that rendered it; when the switch is the one KeySwitchFix asked for,
  the word is kept and repaired to the reading that is a real word
  (`standard`, or `سلام` for `sgام`) live, after the adaptive pause, or at
  Space. The layout switch is also requested before the replacement text is
  injected, so it is queued ahead of the next keystrokes.

### Changed

- Capitalised English words are never spell-corrected (names, acronyms, code).
- Dropping a word's final letter is offered only in Aggressive mode, so a
  plural or inflection missing from the lexicon is never stripped to its stem.
- Spelling correction is skipped below Aggressive in common code editors and
  terminals (identifiers such as `bool` sit one edit from `book`); layout
  repair still runs there.
- The hamza forms `ئ ؤ أ إ ء` are part of the Persian alphabet, key map, and
  confusion sets (`مسول` → `مسئول`).
- The rank tables are a build product: `build-native.sh` generates them when
  absent (requires `pip install wordfreq==3.1.1`), and the CI/release
  workflows install wordfreq. A build without them still works; the dashboard
  reports spelling as unavailable.
- Diagnostics count layout fixes and spelling fixes separately.
- `build-windows.ps1`: a native Windows build script (Python + Zig) that runs
  the same tests and verification as `build-native.sh`.

## [2.8.0] - 2026-09-04

### Added

- A keyboard-hook watchdog. Windows silently detaches a low-level hook whose
  callback ever exceeds its timeout budget; the application now compares
  `GetLastInputInfo` with the last event the hooks delivered and re-arms them,
  instead of reporting `Running` forever while nothing is corrected.
- Zero-width non-joiner support: Shift+Space is treated as a Persian word
  boundary, the exact ZWNJ is replayed after a correction, and phrase repair
  reproduces it. `ld` + Shift+Space + `o,hil` now becomes `می‌خواهم`.
- The ZWNJ halves `می`, `ها`, `تر`, `ام`, and `ات` as recognized short
  Persian words.
- **Exclude _app_.exe** / **Resume correction in _app_.exe** on the tray menu
  for the application you last typed in.
- `Ctrl + Win + K` pauses and resumes correction without opening the tray.
- The dashboard scales with the system DPI setting instead of rendering at
  96 DPI on high-resolution laptops.
- A diagnostics warning when the Persian or English keyboard layout is not
  installed in Windows.
- Regression coverage for English two-key tokens, ZWNJ halves, and the
  Arabic-yeh/kaf normalization; `verify_metadata.py` now asserts that the
  short-word lists in `core.c` and `generate_blooms.py` are identical.

### Changed

- The 500 ms diagnostics refresh runs only while the dashboard is visible,
  and labels are rewritten only when their text changes (no flicker).
- The tray menu posts `WM_NULL` after `TrackPopupMenu`, so it dismisses when
  the user clicks elsewhere.

### Fixed

- `id`, `ms`, and `pr` typed in English were rewritten to `هی`, `پس`, and
  `حق` after a pause or Space, because the English short-word list did not
  contain them and the Persian side therefore counted as one-sided evidence.
  They are now genuine collisions that require sentence context or an explicit
  preference. The trade-off: an isolated `ms` intended as `پس` at the very
  start of a sentence now waits for the next word (phrase repair) or Persian
  document context instead of being corrected on its own.
- Persian text typed on a layout that emits Arabic `ي`/`ك` (U+064A/U+0643)
  looked unknown to the normalized dictionaries, which allowed accidental
  English matches to rewrite correct Persian words. Runtime tokens are now
  canonicalized to `ی`/`ک`.
- Persian words typed with a tanwin or tashdid (`حتماً`, `مدرّس`) were unknown
  to the diacritic-free dictionaries; lookups now strip the marks first, so
  correctly typed words stay protected.
- A ZWNJ produced by a letter key (Shift+B on the legacy Persian layout) is
  treated as the same word boundary as Shift+Space.
- `find_layout` called `LoadKeyboardLayout` on every keystroke whenever one
  language was missing, silently adding a keyboard to the user's language bar.
  The static fallback table is used instead and the diagnostics explain what
  is missing.
- The uninstaller's self-delete helper launched a bare `cmd.exe`, which
  Windows resolves through the current directory first; it now uses the full
  `System32` path.

## [2.7.0] - 2026-07-27

### Added

- Continuous current-sentence monitoring for up to 32 words or 512 characters.
- A pending-word state that preserves live/idle corrections until the following
  Space commits them to sentence history.
- Regression coverage proving Space is a boundary, alternate Windows Persian
  letter keys remain valid, and sentence evaluation is not capped at four words.
- A three-cycle strict implementation review focused on the real keyboard Hook.

### Changed

- Sequence review now retains the beginning of the current sentence and
  continues after a retrospective correction instead of clearing all context.
- Runtime layout translation is gated by an explicit word-key allowlist rather
  than accepting every printable `ToUnicodeEx` result.
- Diagnostics show bounded previews instead of formatting an entire long
  corrected sentence.

### Fixed

- Space was incorrectly accepted as a word token by runtime `ToUnicodeEx`
  translation, preventing the word/sentence boundary evaluator from running.
- Live-corrected words were lost before their following Space and therefore
  never contributed to sentence analysis.
- The first words of a sentence were discarded after a phrase correction.
- The Space fix preserves the alternate `پ/ئ` physical key used by Windows
  **Persian** and **Persian (Standard)** layouts.

## [2.6.0] - 2026-07-27

### Added

- A bounded two-to-four-word sequence evaluator that can use a later word to
  revise an earlier ambiguous wrong-layout word.
- Retrospective phrase repair, including `nv clhkd ;i` → `در زمانی که`.
- Immediate one-key Undo: pressing plain **Backspace** within 15 seconds of an
  automatic correction restores the exact original word or phrase.
- Sequence regressions for coherent Persian and English phrases and a negative
  mixed-language case.

### Changed

- Frequent-word evidence now participates directly in genuine bilingual
  collisions, fixing Persian-layout `اخص` → English `how`.
- Common one-character words (`a`, `I`, and `و`) are recognized by the sequence
  evaluator.
- Phrase history is discarded after caret movement, mouse clicks, unsupported
  punctuation, layout switching, or a foreground-window change.
- The original `Ctrl + Win + Backspace` Undo remains available as a fallback.

### Fixed

- Restored boundary and adaptive-pause recognition for `jh` → `تا`.
- Corrected the stale `2.1` version text on the Setup completion screen.

## [2.5.0] - 2026-07-27

### Added

- A 20,000-word common and 2,000-word frequent layer for each language,
  derived reproducibly from wordfreq 3.1.1.
- Normalized collision priors for the beginning of a sentence.
- Weighted per-window document language that survives sentence boundaries.
- Runtime physical-key translation for both Windows Persian layouts.
- Five-round strict review and adversarial common-word regression matrix.

### Changed

- One-sided dictionary matches now override language preference, fixing
  Persian-layout `اثممخ` → English `hello`.
- Only genuine two-language collisions consult context or tray preference.
- Mid-sentence decisions prioritize the surrounding document language; the
  beginning of a sentence can additionally use a cross-language frequency prior.
- Two-key collisions such as `of`/`خب` require context or explicit preference
  instead of an unsafe corpus-only guess.
- Common-prefix idle guards protect incomplete words such as `بهسا...`.
- Period participates in boundary correction, and Undo feeds the restored
  source language back into the context model.

### Fixed

- `پیدا` and other layout-dependent words now work with both **Persian** and
  **Persian (Standard)** keyboard layouts supplied by Windows.
- A near-threshold sentence-start collision now arms the adaptive timer instead
  of being abandoned before the idle evidence can be evaluated.

## [2.4.0] - 2026-07-27

### Added

- A filtered 4,897-word spoken-English frequency supplement.
- A filtered 616-word common-Persian supplement.
- Regression coverage for `مثل`/`leg`, `really`, and `مرسی`.
- A **Writing language** submenu on the tray icon.

### Changed

- Persian/English collision preference is now labeled for its real purpose,
  instead of appearing to affect only short words.
- Auto sentence intent lasts 90 seconds and survives caret clicks in the same
  window; navigation and manual layout switching still clear it.
- Explicit **Prefer Persian** resolves the valid `leg`/`مثل` collision on the
  third key, while Auto and Prefer English preserve genuine English `leg`.
- Release packages now include the complete notices for every embedded word source.

## [2.3.1] - 2026-07-27

### Fixed

- Recognize common unshifted initial alef-madda spellings such as `ایا`
  through canonical `آیا` dictionary lookup without rewriting the user's spelling.
- Keep correctly typed Persian `ایا` from being misclassified as English.

### Added

- A controlled expansion of common exact two-key Persian words.
- Regression coverage for `ایا`, `اقا`, and `اب`.

## [2.3.0] - 2026-07-27

### Added

- Confidence scoring that combines dictionary membership, word length, prefix
  safety, typing phase, and recent sentence language.
- Exact common two-key Persian and English word recognition.
- Per-window language context that follows recent confirmed words and corrections.
- Auto, Prefer Persian, and Prefer English modes for ambiguous short words.
- Regression coverage for `کمک`, `کن`, `بین`, `تا`, `خب`, and `پیدا`.

### Changed

- Balanced mode now recognizes high-confidence three-key words immediately.
- Two-key words resolve after an adaptive pause without requiring Space.
- Genuine ambiguous words such as English `of` remain unchanged in Auto mode
  unless Persian sentence context supplies enough evidence.

## [2.2.0] - 2026-07-24

### Added

- Immediate correction for unambiguous wrong-layout words without waiting for Space.
- Offline proper-prefix guards for both English and Persian.
- Adaptive pause timing based on the user's measured typing cadence.
- Reproducible prefix-resource generation and expanded regression coverage.

### Changed

- Ambiguous matches wait only while typing is continuing, then resolve after an adaptive pause or a word boundary.
- Duplicate release attempts are now idempotent when the tag and source tree already match.

## [2.1.1] - 2026-07-22

### Fixed

- Restored the standard Windows tooltip for the version 4 tray icon.
- Delayed automatic correction until Space, Enter, or Tab so valid Persian and English words are not changed while they are still being typed.
- Made release packaging independent of executable permission bits on shell scripts.

## [2.1.0] - 2026-07-20

### Added

- Desktop and Start Menu shortcuts created by Setup and removed by Uninstall.
- Automatic tray-icon recovery after Windows Explorer restarts.
- Persian-language project overview.
- Automated source validation, binary validation, CI builds, and release packaging.

### Changed

- Simplified the dashboard and tray menu by removing the user-facing self-test.
- Kept dictionary integrity validation automatic during startup.
- Preserved the native executable at approximately 1.1 MB.

## [2.0.2] - 2026-07-20

### Fixed

- Setup now shows an Installation Complete page and launches the app only after **Finish** is selected.

## [2.0.1] - 2026-07-20

### Fixed

- New installations start with automatic correction enabled.
- The main toggle uses action-oriented **Enable** and **Pause** labels.
- Modifier state is tracked directly from hook events.

## [2.0.0] - 2026-07-19

### Added

- Native x64 Win32 application, per-user Setup, standalone Uninstaller, tray UI, diagnostics, and offline Bloom dictionaries.
- Physical scan-code detection for Persian and English layout mismatches.

[2.7.0]: https://github.com/samaliyan/KeySwitchFix/compare/v2.6.0...v2.7.0
[2.6.0]: https://github.com/samaliyan/KeySwitchFix/compare/v2.5.0...v2.6.0
[2.5.0]: https://github.com/samaliyan/KeySwitchFix/compare/v2.4.0...v2.5.0
[2.4.0]: https://github.com/samaliyan/KeySwitchFix/compare/v2.3.1...v2.4.0
[2.3.1]: https://github.com/samaliyan/KeySwitchFix/compare/v2.3.0...v2.3.1
[2.3.0]: https://github.com/samaliyan/KeySwitchFix/compare/v2.2.0...v2.3.0
[2.2.0]: https://github.com/samaliyan/KeySwitchFix/compare/v2.1.1...v2.2.0
[2.1.1]: https://github.com/samaliyan/KeySwitchFix/compare/v2.1.0...v2.1.1
[2.1.0]: https://github.com/samaliyan/KeySwitchFix/releases/tag/v2.1.0
[2.0.2]: https://github.com/samaliyan/KeySwitchFix/compare/v2.0.2...v2.1.0
[2.0.1]: https://github.com/samaliyan/KeySwitchFix/compare/v2.0.1...v2.0.2
[2.0.0]: https://github.com/samaliyan/KeySwitchFix/releases/tag/v2.0.0
