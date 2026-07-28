# Architecture

KeySwitchFix is a single-process native Win32 application plus a per-user Setup/Uninstaller executable.

## Input pipeline

1. A `WH_KEYBOARD_LL` hook receives physical keyboard events.
2. The foreground thread's keyboard layout is classified as English, Persian, or unsupported.
3. Physical scan codes are translated through the actual installed/last-used
   Windows English and Persian layouts. The static core table is only a safe
   fallback, so both Persian layout variants are supported.
4. The current word is checked against compact offline base, common-word, and
   proper-prefix Bloom dictionaries; exact lists cover common two-key words. A controlled
   initial `ا` → `آ` canonical lookup recognizes common unshifted Persian
   spellings without modifying the replacement text.
5. A deterministic confidence score combines both candidate memberships, word
   length, prefix safety, evaluation phase, and recent per-window sentence language.
6. A high-confidence mismatch is corrected immediately. Two-key words and valid
   active-language prefixes wait for an adaptive pause or a Space, Enter, or Tab
   boundary.
7. Auto mode learns 90-second per-window document language from confirmed words
   and corrections. Sentence punctuation resets position but preserves that
   weighted evidence. Caret clicks inside the same window preserve it;
   navigation and manual layout switches clear it. Explicit Persian/English
   preference resolves an ambiguous word when automatic evidence is insufficient.
8. A bounded history keeps the current sentence from its beginning, up to 32
   words or 512 characters. At every Space, the sentence and its suffixes are
   rescored in both layouts. A complete coherent run can retrospectively repair
   earlier words; mixed-language runs and partial dictionary matches are left
   unchanged. A word corrected live is held as pending until Space commits it,
   so it is not lost from sentence history.
9. The original text is replaced with marked `SendInput` events and the target
   window is asked to switch layout.
10. The exact original and replacement are retained for 15 seconds. One plain
    Backspace restores the original word or phrase; the registered
    `Ctrl + Win + Backspace` hotkey is a fallback.

The application ignores its own injected input, rejects Space/digits as word
tokens before runtime `ToUnicodeEx` mapping, clears phrase history whenever
the caret model can no longer be trusted, and limits each candidate word to
`KS_MAX_WORD` tokens, each sentence to `KS_MAX_SEQUENCE_WORDS`, and
reconstructed text to `KS_MAX_SEQUENCE_CHARS`.

## Components

| Component | Responsibility |
| --- | --- |
| `src/core.c` | fallback scan-code mapping, membership, word/sequence scoring, decisions |
| `src/app.c` | hooks, bounded phrase history, Undo, correction, settings, tray, dashboard |
| `src/installer.c` | per-user install, shortcuts, startup, registration, uninstall |
| `resources/*.bloom` | compact offline base, common-word, and proper-prefix membership resources |
| `tools/generate_blooms.py` | reproducible dictionary normalization and Bloom-resource generation |
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
- An isolated collision such as English `leg` versus Persian `مثل` has no
  unique automatic answer; Auto mode requires sentence context, while the
  preference setting lets the user choose a deterministic default.
