#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"

cd "$ROOT"
bash ./build-native.sh

cd dist
sha256sum -c SHA256SUMS.txt
zip -9 -j "KeySwitchFix-${VERSION}-Windows-x64.zip" \
  KeySwitchFix-Setup.exe \
  KeySwitchFix-Uninstall.exe \
  KeySwitchFix.exe \
  SHA256SUMS.txt \
  ../README.md \
  ../LICENSE.txt \
  ../third-party/README.txt

unzip -t "KeySwitchFix-${VERSION}-Windows-x64.zip"
