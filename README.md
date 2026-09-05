<div align="center">
  <img src="resources/app-icon.png" width="96" alt="KeySwitchFix icon">
  <h1>KeySwitchFix</h1>
  <p>Lightweight, private, automatic Persian ↔ English keyboard layout repair and spelling correction for Windows.</p>

  [![CI](https://github.com/silimore/KeySwitchFix/actions/workflows/ci.yml/badge.svg)](https://github.com/silimore/KeySwitchFix/actions/workflows/ci.yml)
  [![Latest release](https://img.shields.io/github/v/release/silimore/KeySwitchFix)](https://github.com/silimore/KeySwitchFix/releases/latest)
  [![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.txt)
  [![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4.svg)](#requirements)

  [Download](https://github.com/silimore/KeySwitchFix/releases/latest) · [Persian README](README_FA.md) · [Report a bug](https://github.com/silimore/KeySwitchFix/issues/new?template=bug_report.yml)
</div>

## What it does

KeySwitchFix notices when a word or sentence fragment was typed using the wrong
Persian or English keyboard layout, replaces it, and switches the target
application's layout automatically.

| Physical keys | Wrong output | Corrected output |
| --- | --- | --- |
| `password` | `حشسسصخقی` | `password` |
| `sghl` | `sghl` | `سلام` |
| `;jhf` | `;jhf` | `کتاب` |
| `nv clhkd ;i` | `nv clhkd ;i` | `در زمانی که` |

Detection is confidence-based: the intended word must exist in the opposite-language dictionary while the text produced by the active layout must not. Proper-prefix guards prevent valid words from being changed while they are still being typed. Unambiguous mistakes are corrected immediately; ambiguous matches are checked after an adaptive typing pause or at Space, Enter, or Tab.

Version 2.9 adds offline spelling correction for both languages: `نسحه` →
`نسخه`, `teh` → `the`. A noisy-channel model scores every candidate within
one edit by corpus frequency and by how people actually mistype (transposed
letters, neighbouring keys, doubled letters, Persian homophones). It runs at
the word boundary, only for words unknown in both layouts, and one Backspace
undoes it and remembers the spelling. See [Spelling](docs/SPELLING.md).

Version 2.8 adds a keyboard-hook watchdog, Shift+Space (ZWNJ) awareness for
words such as `می‌خواهم`, a per-app exclusion shortcut on the tray menu, a
`Ctrl + Win + K` pause hotkey, and a DPI-aware dashboard.

Version 2.7 combines both dictionary candidates, common/frequent vocabulary,
sentence position, weighted document language, normalized bilingual frequency,
typing phase, prefix safety, and continuous current-sentence review. It keeps
the sentence from its beginning (up to 32 words or 512 characters), reevaluates
it at each Space, and can use a later word to revise earlier ambiguous text.
An explicit preference remains available for collisions that cannot be
inferred from the available keys or context alone.

## Highlights

- Native Win32 C application with no .NET or external runtime
- Offline spelling correction for Persian and English with Off / Conservative / Balanced / Aggressive levels
- Half-space (`می‌پرسیدند`) and missing-space (`in the`, `در خانه`) repair
- Learns your own vocabulary: a word typed twice is never "corrected"; an optional personal dictionary keeps undone words across restarts
- Effective union of 159,852 offline English and Persian spellings
- 20,000 common and 2,000 frequent entries per language from wordfreq 3.1.1
- Works across desktop applications using physical scan-code mapping
- Supports both Windows **Persian** and **Persian (Standard)** key layouts
- Corrects clear mistakes while typing, without waiting for Space
- Protects valid longer words with offline prefix dictionaries and an adaptive pause
- Recognizes common two-key and three-key words using sentence context
- Re-evaluates the current sentence from its beginning, so later evidence can repair earlier words
- Auto, Prefer Persian, and Prefer English modes for ambiguous collisions
- Recognizes common unshifted initial `آ` spellings such as `ایا`
- Undo the latest correction with one plain **Backspace**; `Ctrl + Win + Backspace` remains a fallback
- Treats Shift+Space as a Persian ZWNJ boundary, so `می‌خواهم` and `کتاب‌ها` are repaired and re-typed exactly
- Re-arms its keyboard hook automatically if Windows silently detaches it
- **Exclude this app** from the tray menu and `Ctrl + Win + K` to pause/resume
- English-only dashboard, tray controls, sensitivity settings, and live diagnostics
- Per-user installer, desktop/Start Menu shortcuts, startup option, and clean uninstaller
- No network access, telemetry, cloud processing, typed-text log, or background service
- Compact native executable with all word resources embedded

## Install

1. Open the [latest release](https://github.com/silimore/KeySwitchFix/releases/latest).
2. Download `KeySwitchFix-Setup.exe`.
3. Run Setup and select **Install**. Administrator access is not required.
4. Select **Finish** to close Setup and launch KeySwitchFix.

The executable is currently unsigned, so Microsoft Defender SmartScreen may display an unknown-publisher warning. Review the source and release checksum before choosing **Run anyway**.

## Use

- KeySwitchFix starts enabled and can start automatically with Windows.
- Double-click the tray icon or use the desktop shortcut to open the dashboard.
- Right-click the tray icon to pause correction, choose **Writing language**, toggle **Fix spelling mistakes**, exclude the app you last typed in, or exit.
- Press `Ctrl + Win + K` to pause or resume correction from anywhere.
- Closing the dashboard hides it to the tray; choosing **Exit** stops the program.
- If Windows Explorer restarts, the tray icon restores itself automatically.

If correction does not occur, open **Live diagnostics** and confirm:

- `Protection is active`
- `Keyboard hook: Running`
- `Current context` reports an English or Persian layout
- `Input observed` increases while typing in another application
- No warning about a missing Persian or English keyboard layout is shown

## Privacy and security

KeySwitchFix processes only the current word and the current sentence in
memory, bounded to 32 words or 512 characters. That history is discarded on
caret movement, mouse clicks, window changes, sentence termination, or
unsupported punctuation. It does not include network code and never stores
typed text. Standard Win32 password fields and common password-manager
processes are skipped. See
[Privacy design](docs/PRIVACY.md) and [Security policy](SECURITY.md).

Windows prevents lower-integrity processes from injecting input into elevated applications. If a target application runs as administrator, KeySwitchFix must run at the same integrity level to edit it.

## Requirements

- Windows 10 or Windows 11, x64
- English and Persian keyboard layouts installed in Windows

## Build from source

The reproducible cross-build uses GCC for native core tests and Zig for the Windows x64 binaries:

```bash
npm install --prefix /tmp/keyswitchfix-zig @oven/zig-linux-x64@0.12.0-dev.1286
ZIG=/tmp/keyswitchfix-zig/node_modules/@oven/zig-linux-x64/zig ./build-native.sh
```

Required tools: Bash, GCC, Python 3 with `wordfreq==3.1.1` (for the spelling rank tables), npm, and `zip` for release packaging.

On Windows, `build-windows.ps1` performs the same steps natively (Python 3 and a
Zig 0.12+ zip extracted to `C:\zig` are the only requirements):

```powershell
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
```

The build performs:

- strict C compilation with `-Wall -Wextra -Werror`
- generation of the spelling rank tables from wordfreq when they are absent
- positive and negative dictionary/mapping tests and the spelling test suite
- x64 Windows GUI PE validation
- exact verification of embedded dictionaries and Setup payloads

See [Architecture](docs/ARCHITECTURE.md), the
[three-round 2.9 review](docs/STRICT_REVIEW_2.9.0.md), the
[2.8 review](docs/REVIEW_2.8.0.md), the
[three-cycle 2.7 review](docs/STRICT_REVIEW_2.7.0.md), and
[Contributing](CONTRIBUTING.md) before submitting changes.

## Uninstall

Use **Windows Settings → Apps → Installed apps → KeySwitchFix → Uninstall**. The uninstaller removes the application, startup entry, Installed Apps registration, and KeySwitchFix shortcuts. You can choose whether to keep personal settings.

## License

Application code is released under the [MIT License](LICENSE.txt). Embedded
dictionary and frequency resources have their respective notices in
[`third-party/`](third-party/README.txt).

The physical keys for Persian `مثل` also spell valid English `leg`; similarly,
`of` maps to Persian `خب`. Auto mode uses weighted document language and
sentence position. A two-key collision is never guessed from frequency alone.
If Persian should win even for an isolated collision, right-click the tray icon
and choose **Writing language → Prefer Persian for collisions**.
