# Privacy Design

KeySwitchFix is deliberately offline.

## Data processed

The application keeps physical key tokens for the current candidate word and
the current Space-separated sentence in process memory, with hard limits of 32
words and 512 characters. It also keeps the exact latest correction for a
short time so it can be undone (5 seconds for Backspace, 15 seconds for
`Ctrl + Win + Backspace`, and only in the same field). Sentence history is discarded when the
caret model becomes unreliable, including mouse clicks, navigation, window
changes, sentence termination, and unsupported punctuation. The application
also reads the foreground executable name and keyboard layout to apply
exclusions and select the correct mapping.

## Data not collected

KeySwitchFix does not:

- connect to the internet;
- include analytics, telemetry, advertisements, or crash uploaders;
- write typed text, sentences or correction history to files, the registry,
  or the Windows Event Log (the two opt-in exceptions, both off by default,
  are listed below and store single words only);
- run a Windows service;
- transmit process names or settings.

Persistent settings (`settings.ini`) contain only: enabled state, sensitivity, writing-language mode, the two chosen languages (codes such as `en` and `fa`), spelling level and the level restored by the tray toggle, the personal-dictionary switch, the typing-helper switches (digits, punctuation, Persian letters, capitalisation, snippets), the writing-memory and IT-vocabulary switches, the startup preference, and the user-maintained excluded-process list. `stats.ini` holds counters only (fixes and keys today and in total, active days, the date of the current day); the most-corrected words shown on the dashboard live in process memory. `snippets.txt` is written by the user; the application creates the commented template, reads the file, and rewrites it once when it still uses the escapes of versions 3.0 and 3.1 (`\n`, `\t` become `{n}`, `{t}`), keeping the previous file as `snippets.txt.bak`. The optional writing memory (off by default) stores, in `writing-memory.txt`, letters-only words the user finished with Space at least twice and the hand repairs they made, with counts; never in identifiable password fields, excluded apps, developer tools, terminals or remote sessions, and never at Enter or Tab boundaries. It can be opened, edited, or deleted from the tray menu. The `Ctrl + Win + X` clean-up uses the clipboard for the duration of one copy-and-paste and restores the previous clipboard content afterwards; KeySwitchFix keeps nothing. The text it puts on the clipboard (the cleaned selection, and the restored previous content) is marked so that Windows clipboard history and cloud clipboard skip it; the copy the target application itself makes when the selection is copied cannot be marked, so with clipboard history on (`Win + V`) the original selection can appear there, as with any Ctrl+C. The spelling ignore list (64 words) and the learned vocabulary (1,024 words the user typed that no dictionary knows) live in process memory only. The personal dictionary is opt-in and off by default; when enabled, only words whose correction the user explicitly undid are added to `personal-dictionary.txt` in the settings folder (appended, or the file is rewritten whole with the same words when it is cleaned up). Language packs (`.kslang`) are read-only word lists: the application reads them from its own `languages` folder and from `%LOCALAPPDATA%\KeySwitchFix\languages`, and never writes them.

## Uninstalling

The uninstaller asks whether to remove your data; the default answer keeps
it, so a reinstall picks up your settings, snippets and learned words. Answer
**Yes** (or run the uninstaller with `/purge`) to delete everything in
`%LOCALAPPDATA%\KeySwitchFix`, including `writing-memory.txt`. A silent
uninstall (`/silent`, used by package managers) keeps the data unless `/purge`
is added. **Forget everything learned** on the tray menu deletes the writing
memory at any time.

## Sensitive fields

Standard Win32 password edits, password fields that report the protected state through Microsoft Active Accessibility (Chrome, Edge and Firefox do), common password-manager processes, and remote-desktop clients are excluded. Some browsers and custom UI frameworks do not expose password status through standard Win32 controls, so users should pause or exit KeySwitchFix when entering particularly sensitive text in an application whose field type cannot be verified.

## Verifiability

There are no networking libraries in the application import table. Release builds and their SHA-256 checksums are produced by the public GitHub Actions workflow.
