# Changelog

All notable changes to KeySwitchFix are documented here. The project follows [Semantic Versioning](https://semver.org/).

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

[2.7.0]: https://github.com/silimore/KeySwitchFix/compare/v2.6.0...v2.7.0
[2.6.0]: https://github.com/silimore/KeySwitchFix/compare/v2.5.0...v2.6.0
[2.5.0]: https://github.com/silimore/KeySwitchFix/compare/v2.4.0...v2.5.0
[2.4.0]: https://github.com/silimore/KeySwitchFix/compare/v2.3.1...v2.4.0
[2.3.1]: https://github.com/silimore/KeySwitchFix/compare/v2.3.0...v2.3.1
[2.3.0]: https://github.com/silimore/KeySwitchFix/compare/v2.2.0...v2.3.0
[2.2.0]: https://github.com/silimore/KeySwitchFix/compare/v2.1.1...v2.2.0
[2.1.1]: https://github.com/silimore/KeySwitchFix/compare/v2.1.0...v2.1.1
[2.1.0]: https://github.com/silimore/KeySwitchFix/releases/tag/v2.1.0
[2.0.2]: https://github.com/silimore/KeySwitchFix/compare/v2.0.2...v2.1.0
[2.0.1]: https://github.com/silimore/KeySwitchFix/compare/v2.0.1...v2.0.2
[2.0.0]: https://github.com/silimore/KeySwitchFix/releases/tag/v2.0.0
