# Architecture

KeySwitchFix is a single-process native Win32 application plus a per-user Setup/Uninstaller executable.

## Input pipeline

1. A `WH_KEYBOARD_LL` hook receives physical keyboard events.
2. The foreground thread's keyboard layout is classified as English, Persian, or unsupported.
3. Physical scan codes are mapped to parallel English and Persian candidate characters.
4. The current word is checked against compact offline Bloom dictionaries.
5. A correction is accepted only when the opposite-layout candidate is known and the active-layout candidate is unknown.
6. The original text is replaced with marked `SendInput` events and the target window is asked to switch layout.

The application ignores its own injected input, clears context on navigation and mouse actions, and limits each candidate word to `KS_MAX_WORD` tokens.

## Components

| Component | Responsibility |
| --- | --- |
| `src/core.c` | scan-code mapping, Bloom parsing, language candidates, decisions |
| `src/app.c` | hooks, foreground context, correction, settings, tray, dashboard |
| `src/installer.c` | per-user install, shortcuts, startup, registration, uninstall |
| `resources/*.bloom` | compact offline dictionary membership resources |
| `tests/core_tests.c` | native positive and negative detection tests |
| `tests/verify_pe.py` | x64 GUI PE and embedded-payload verification |

## Installer model

Setup installs under `%LOCALAPPDATA%\Programs\KeySwitchFix`, stores settings under `%LOCALAPPDATA%\KeySwitchFix`, registers with Installed Apps under `HKCU`, and optionally adds an `HKCU` startup entry. Administrator privileges are not requested.

The standalone Uninstaller and Installed Apps entry use the same uninstall implementation. The uninstaller only terminates the executable at the resolved installation path, not arbitrary processes with a matching filename.

## Constraints

- The application supports Windows x64 and the standard English and Persian layouts.
- Custom browser password controls cannot always be identified through standard Win32 edit styles.
- `SendInput` follows Windows integrity-level restrictions.
- Bloom dictionaries have a small probabilistic false-positive rate, mitigated by requiring an opposite candidate match and active candidate miss.

