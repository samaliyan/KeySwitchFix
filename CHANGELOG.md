# Changelog

All notable changes to KeySwitchFix are documented here. The project follows [Semantic Versioning](https://semver.org/).

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

[2.1.1]: https://github.com/silimore/KeySwitchFix/compare/v2.1.0...v2.1.1
[2.1.0]: https://github.com/silimore/KeySwitchFix/releases/tag/v2.1.0
[2.0.2]: https://github.com/silimore/KeySwitchFix/compare/v2.0.2...v2.1.0
[2.0.1]: https://github.com/silimore/KeySwitchFix/compare/v2.0.1...v2.0.2
[2.0.0]: https://github.com/silimore/KeySwitchFix/releases/tag/v2.0.0
