#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
ZIG="${ZIG:-zig}"
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-/tmp/keyswitchfix-zig-global}"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-/tmp/keyswitchfix-zig-local}"

cd "$ROOT"
mkdir -p dist

python3 tests/verify_metadata.py

gcc -std=c11 -Wall -Wextra -Werror -O2 src/core.c tests/core_tests.c -o /tmp/keyswitchfix-core-tests
/tmp/keyswitchfix-core-tests

cd resources
"$ZIG" rc /:auto-includes gnu /c 65001 /fo app.res app.rc
cd ..
"$ZIG" cc -target x86_64-windows-gnu -DUNICODE -D_UNICODE -std=c11 -O2 \
  -Wall -Wextra -Werror -Isrc -Iresources src/app.c src/core.c resources/app.res \
  -o dist/KeySwitchFix.exe -luser32 -lgdi32 -lcomctl32 -lshell32 -ladvapi32 \
  -Wl,/subsystem:windows

cd resources
"$ZIG" rc /:auto-includes gnu /c 65001 /fo uninstaller.res uninstaller.rc
cd ..
"$ZIG" cc -target x86_64-windows-gnu -DUNICODE -D_UNICODE -std=c11 -O2 \
  -Wall -Wextra -Werror -Iresources src/installer.c resources/uninstaller.res \
  -o dist/KeySwitchFix-Uninstall.exe -luser32 -lshell32 -ladvapi32 -lole32 -luuid \
  -Wl,/subsystem:windows

cd resources
"$ZIG" rc /:auto-includes gnu /c 65001 /fo installer.res installer.rc
cd ..
"$ZIG" cc -target x86_64-windows-gnu -DUNICODE -D_UNICODE -std=c11 -O2 \
  -Wall -Wextra -Werror -Iresources src/installer.c resources/installer.res \
  -o dist/KeySwitchFix-Setup.exe -luser32 -lshell32 -ladvapi32 -lole32 -luuid \
  -Wl,/subsystem:windows

python3 tests/verify_pe.py
cd dist
sha256sum KeySwitchFix-Setup.exe KeySwitchFix-Uninstall.exe KeySwitchFix.exe > SHA256SUMS.txt
cat SHA256SUMS.txt
