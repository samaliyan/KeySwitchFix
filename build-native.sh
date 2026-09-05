#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
ZIG="${ZIG:-zig}"
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-/tmp/keyswitchfix-zig-global}"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-/tmp/keyswitchfix-zig-local}"

cd "$ROOT"
mkdir -p dist

# Spelling correction needs the wordfreq-derived rank tables. They are not
# committed (they are a build product of wordfreq 3.1.1); generate them when
# absent. `pip install wordfreq==3.1.1` is required for that step.
if [ ! -f resources/en-rank.bin ] || [ ! -f resources/fa-rank.bin ]; then
  python3 tools/generate_rank_tables.py --if-missing
fi

python3 tests/verify_metadata.py

gcc -std=c11 -Wall -Wextra -Werror -O2 src/core.c tests/core_tests.c -o /tmp/keyswitchfix-core-tests
/tmp/keyswitchfix-core-tests
gcc -std=c11 -Wall -Wextra -Werror -O2 src/core.c src/spell.c tests/spell_tests.c -o /tmp/keyswitchfix-spell-tests
/tmp/keyswitchfix-spell-tests

cd resources
"$ZIG" rc /:auto-includes gnu /c 65001 /fo app.res app.rc
cd ..
"$ZIG" cc -target x86_64-windows-gnu -DUNICODE -D_UNICODE -std=c11 -O2 \
  -Wall -Wextra -Werror -Isrc -Iresources src/app.c src/core.c src/spell.c resources/app.res \
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
