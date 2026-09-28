# Strict review, 3.2.0

Three rounds of review. In each round, independent reviewers scored five areas
out of 100 and listed defects. The defects were then fixed before the next
round. A final verification pass scored the released code.

| Area | Round 1 (3.1.0) | Round 2 | Round 3 | Final (3.2.0) |
| --- | --- | --- | --- | --- |
| Correction engine | 60 | 72 | 76 | 86 |
| Dashboard and tray UI | 45 | 64 | 81 | 87 |
| Language accuracy | 72 | 70 | 66 | 79 |
| Code robustness | 74 | 86 | 88 | 90 |
| Installer, build, docs | 50 | 66 | 76 | 82 |
| **Average** | **60** | **72** | **77** | **85** |

## Main fixes

**Engine**

- Password fields are detected through MSAA. The check is deferred when the engine is busy.
- Every engine operation carries a re-entrancy guard. Interrupted operations never inject input.
- Undo works only in the same field, and never after Enter or Tab.
- Remote-desktop windows are skipped.
- Excel AutoComplete handling is safe while a cell is being edited.
- Ctrl+Win+X is aborted when focus changes or a key is pressed.

**UI**

- A new four-page dashboard, with settings that apply instantly.
- A per-monitor DPI manifest; the window rebuilds for each monitor's DPI, and the footer is always visible.
- Full keyboard access and high-contrast support.
- Accessible page tabs.
- A grey tray icon while paused, and fewer notifications.

**Language**

- Abbreviations and pack words are protected.
- Short English words are repaired inside Persian text.
- The corpus tests cover contexts in both directions and set false-positive and recall thresholds.

**Installer**

- On uninstall, keeping your data is the default. `/purge` removes it.
- Your startup choice is kept across upgrades.
- Cleanup of a running file is done safely at the next sign-in.
- A manifest is embedded in all three executables.

## Known limits

- The Windows build is type-checked here against stub headers. The real
  build and the PE and manifest checks run in CI.
- The spelling rank tables need `wordfreq`. They are generated at build time.
