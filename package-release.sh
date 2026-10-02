#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"
ZIP="KeySwitchFix-${VERSION}-Windows-x64.zip"

cd "$ROOT"
# A release always carries the language packs.
KSF_REQUIRE_LANGUAGE_PACKS=1 bash ./build-native.sh

cd dist
sha256sum -c SHA256SUMS.txt
if ! ls languages/*.kslang > /dev/null 2>&1; then
  echo "error: no language packs were built" >&2
  exit 1
fi
rm -f "$ZIP" "$ZIP.sha256"
# The program files at the top, the language packs in languages\ (next to
# KeySwitchFix.exe, where a copy run without Setup finds them).
zip -9 -X -j "$ZIP" \
  KeySwitchFix-Setup.exe \
  KeySwitchFix-Uninstall.exe \
  KeySwitchFix.exe \
  SHA256SUMS.txt \
  ../README.md \
  ../README_FA.md \
  ../LICENSE.txt \
  ../third-party/README.txt \
  ../third-party/dictionary-en-LICENSE.txt \
  ../third-party/dictionary-fa-LICENSE.txt \
  ../third-party/wordfreq-LICENSE.txt \
  ../third-party/wordfreq-NOTICE.txt
zip -9 -X "$ZIP" languages/*.kslang

unzip -t "$ZIP"
sha256sum "$ZIP" > "$ZIP.sha256"
cat "$ZIP.sha256"
