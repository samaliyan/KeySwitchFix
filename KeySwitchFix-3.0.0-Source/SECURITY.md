# Security Policy

## Supported version

Security fixes are provided for the latest release of KeySwitchFix.

## Reporting a vulnerability

Please do not open a public issue for a suspected vulnerability involving keyboard input, password-field detection, privilege boundaries, or installer behavior.

Use GitHub's **Security → Report a vulnerability** flow to open a private security advisory for this repository. Include:

- affected KeySwitchFix version and Windows version;
- target application and integrity level;
- minimal reproduction steps;
- expected and observed behavior;
- logs or screenshots that do not contain secrets or typed private text.

You should receive an initial acknowledgement within seven days. A validated issue will be fixed and disclosed through a release advisory after a safe update is available.

## Security boundaries

- KeySwitchFix is not a credential manager and does not guarantee detection of every custom browser or framework password field.
- Windows UIPI prevents a normal process from editing an elevated target. This is expected operating-system behavior.
- Release executables are currently unsigned. Verify SHA-256 checksums published with each release.

