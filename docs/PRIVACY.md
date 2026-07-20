# Privacy Design

KeySwitchFix is deliberately offline.

## Data processed

The application keeps physical key tokens for only the current candidate word in process memory. It also reads the foreground executable name and keyboard layout to apply exclusions and select the correct mapping.

## Data not collected

KeySwitchFix does not:

- connect to the internet;
- include analytics, telemetry, advertisements, or crash uploaders;
- write typed words to files, the registry, or Windows Event Log;
- store a history of corrections;
- run a Windows service;
- transmit process names or settings.

Persistent settings contain only enabled state, sensitivity, startup preference, and the user-maintained excluded-process list.

## Sensitive fields

Standard Win32 password edits and common password-manager processes are excluded. Some browsers and custom UI frameworks do not expose password status through standard Win32 controls, so users should pause or exit KeySwitchFix when entering particularly sensitive text in an application whose field type cannot be verified.

## Verifiability

There are no networking libraries in the application import table. Release builds and their SHA-256 checksums are produced by the public GitHub Actions workflow.

