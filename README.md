<div align="center">
  <img src="resources/app-icon.png" width="96" alt="KeySwitchFix icon">
  <h1>KeySwitchFix</h1>
  <p>Lightweight, private, automatic Persian ↔ English keyboard layout repair for Windows.</p>

  [![CI](https://github.com/silimore/KeySwitchFix/actions/workflows/ci.yml/badge.svg)](https://github.com/silimore/KeySwitchFix/actions/workflows/ci.yml)
  [![Latest release](https://img.shields.io/github/v/release/silimore/KeySwitchFix)](https://github.com/silimore/KeySwitchFix/releases/latest)
  [![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.txt)
  [![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4.svg)](#requirements)

  [Download](https://github.com/silimore/KeySwitchFix/releases/latest) · [Persian README](README_FA.md) · [Report a bug](https://github.com/silimore/KeySwitchFix/issues/new?template=bug_report.yml)
</div>

## What it does

KeySwitchFix notices when a word was typed using the wrong Persian or English keyboard layout, replaces that word, and switches the target application's layout automatically.

| Physical keys | Wrong output | Corrected output |
| --- | --- | --- |
| `password` | `حشسسصخقی` | `password` |
| `sghl` | `sghl` | `سلام` |
| `;jhf` | `;jhf` | `کتاب` |

Detection is confidence-based: the intended word must exist in the opposite-language dictionary while the text produced by the active layout must not. This reduces unwanted corrections.

## Highlights

- Native Win32 C application with no .NET or external runtime
- Approximately 147,000 offline English and Persian dictionary entries
- Works across desktop applications using physical scan-code mapping
- Corrects only after Space, Enter, or Tab so valid words are not changed mid-typing
- Undo the latest correction with **Ctrl + Win + Backspace**
- English-only dashboard, tray controls, sensitivity settings, and live diagnostics
- Per-user installer, desktop/Start Menu shortcuts, startup option, and clean uninstaller
- No network access, telemetry, cloud processing, typed-text log, or background service
- About 1.1 MB for the main executable

## Install

1. Open the [latest release](https://github.com/silimore/KeySwitchFix/releases/latest).
2. Download `KeySwitchFix-Setup.exe`.
3. Run Setup and select **Install**. Administrator access is not required.
4. Select **Finish** to close Setup and launch KeySwitchFix.

The executable is currently unsigned, so Microsoft Defender SmartScreen may display an unknown-publisher warning. Review the source and release checksum before choosing **Run anyway**.

## Use

- KeySwitchFix starts enabled and can start automatically with Windows.
- Double-click the tray icon or use the desktop shortcut to open the dashboard.
- Right-click the tray icon to pause correction or exit.
- Closing the dashboard hides it to the tray; choosing **Exit** stops the program.
- If Windows Explorer restarts, the tray icon restores itself automatically.

If correction does not occur, open **Live diagnostics** and confirm:

- `Protection is active`
- `Keyboard hook: Running`
- `Current context` reports an English or Persian layout
- `Input observed` increases while typing in another application

## Privacy and security

KeySwitchFix processes only the current word in memory. It does not include network code and never stores typed text. Standard Win32 password fields and common password-manager processes are skipped. See [Privacy design](docs/PRIVACY.md) and [Security policy](SECURITY.md).

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

Required tools: Bash, GCC, Python 3, npm, and `zip` for release packaging.

The build performs:

- strict C compilation with `-Wall -Wextra -Werror`
- positive and negative dictionary/mapping tests
- x64 Windows GUI PE validation
- exact verification of embedded dictionaries and Setup payloads

See [Architecture](docs/ARCHITECTURE.md) for the design and [Contributing](CONTRIBUTING.md) before submitting changes.

## Uninstall

Use **Windows Settings → Apps → Installed apps → KeySwitchFix → Uninstall**. The uninstaller removes the application, startup entry, Installed Apps registration, and KeySwitchFix shortcuts. You can choose whether to keep personal settings.

## License

Application code is released under the [MIT License](LICENSE.txt). Embedded dictionary resources have their own compatible notices in [`third-party/`](third-party/README.txt).
