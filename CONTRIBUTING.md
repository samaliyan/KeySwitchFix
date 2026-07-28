# Contributing to KeySwitchFix

Thank you for improving KeySwitchFix. Small, focused changes with tests are preferred.

## Development setup

Required tools:

- Bash, GCC, and Python 3 for native core tests;
- Zig `0.12.0-dev.1286` with the x86_64 Windows GNU target;
- `zip` for release packaging.

Build and validate:

```bash
npm install --prefix /tmp/keyswitchfix-zig @oven/zig-linux-x64@0.12.0-dev.1286
ZIG=/tmp/keyswitchfix-zig/node_modules/@oven/zig-linux-x64/zig ./build-native.sh
```

## Pull requests

1. Open an issue first for broad behavior or dictionary-policy changes.
2. Keep unrelated changes in separate pull requests.
3. Add or update tests for detection and mapping behavior.
4. Run `./build-native.sh` before submitting.
5. Explain user impact, privacy impact, and validation in the pull request.

## Detection changes

False corrections are more disruptive than missed corrections. A dictionary change must preserve the rule that the intended opposite-layout word is known and the active-layout output is unknown. Include both positive and negative tests.

Do not add raw word lists without confirming their license. Dictionary resources are compact Bloom filters and their upstream notices must remain in `third-party/`.

To reproduce every Bloom resource, install the pinned source packages and run:

```bash
npm install --prefix /tmp/keyswitchfix-dictionaries dictionary-en@4.0.0 dictionary-fa@2.0.0
python3 -m pip install --target /tmp/keyswitchfix-wordfreq wordfreq==3.1.1
PYTHONPATH=/tmp/keyswitchfix-wordfreq python3 tools/generate_blooms.py \
  --dictionary-root /tmp/keyswitchfix-dictionaries/node_modules
```

The generator verifies the exact SHA-256 of the pinned English and Persian
wordfreq data before changing a resource.

## Coding style

- C11, four-space indentation, no tabs.
- Treat project warnings as errors.
- Keep hook callbacks bounded and avoid network, disk, or long blocking operations.
- Preserve English UI text and offline-only operation.
- Avoid adding runtimes, services, telemetry, or background updaters.

By contributing, you agree that your contribution is licensed under the MIT License.
