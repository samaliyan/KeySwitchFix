# Privacy Design

KeySwitchFix is deliberately offline.

## Data processed

The application keeps physical key tokens for the current candidate word and
the current Space-separated sentence in process memory, with hard limits of 32
words and 512 characters. It also keeps the exact latest correction for up to
15 seconds so Backspace can restore it. Sentence history is discarded when the
caret model becomes unreliable, including mouse clicks, navigation, window
changes, sentence termination, and unsupported punctuation. The application
also reads the foreground executable name and keyboard layout to apply
exclusions and select the correct mapping.

## Data not collected

KeySwitchFix does not:

- connect to the internet;
- include analytics, telemetry, advertisements, or crash uploaders;
- write typed words to files, the registry, or Windows Event Log;
- persist typed text or correction history beyond process memory;
- run a Windows service;
- transmit process names or settings.

Persistent settings contain only enabled state, sensitivity, startup preference, and the user-maintained excluded-process list.

## Sensitive fields

Standard Win32 password edits and common password-manager processes are excluded. Some browsers and custom UI frameworks do not expose password status through standard Win32 controls, so users should pause or exit KeySwitchFix when entering particularly sensitive text in an application whose field type cannot be verified.

## Verifiability

There are no networking libraries in the application import table. Release builds and their SHA-256 checksums are produced by the public GitHub Actions workflow.
