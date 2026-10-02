#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
ZIG="${ZIG:-zig}"
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-/tmp/keyswitchfix-zig-global}"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-/tmp/keyswitchfix-zig-local}"
# KSF_REQUIRE_LANGUAGE_PACKS=1 (the release) fails the build when the
# language packs cannot be generated; otherwise a build without wordfreq
# simply offers English and Persian only.
REQUIRE_PACKS="${KSF_REQUIRE_LANGUAGE_PACKS:-0}"
TMP="${TMPDIR:-/tmp}/keyswitchfix-tests"

cd "$ROOT"
mkdir -p dist "$TMP"

# Spelling correction needs the wordfreq-derived rank tables. They are not
# committed (they are a build product of wordfreq 3.1.1); generate them when
# absent. `pip install wordfreq==3.1.1` is required for that step.
if [ ! -f resources/en-rank.bin ] || [ ! -f resources/fa-rank.bin ]; then
  python3 tools/generate_rank_tables.py --if-missing
fi

python3 tools/generate_domain_words.py
python3 tests/verify_metadata.py

# Language packs (tools/build_language_pack.py, wordfreq 3.1.1), bundled
# for Setup. A stale folder from an earlier build never leaks into this one.
rm -rf dist/languages
mkdir -p dist/languages
if [ "$REQUIRE_PACKS" = "1" ]; then
  python3 tools/build_language_pack.py --out dist/languages
else
  python3 tools/build_language_pack.py --out dist/languages --if-available
fi
python3 tools/build_language_pack.py --bundle dist/languages --bundle-out dist/languages.bundle
# Every pack must load with the app's own parser.
gcc -std=c11 -Wall -Wextra -Werror -O2 src/core.c tests/pack_check.c -o "$TMP/pack-check"
if ls dist/languages/*.kslang > /dev/null 2>&1; then
  "$TMP/pack-check" dist/languages/*.kslang
fi

# ---- Tests -------------------------------------------------------------------
# Every suite runs twice: optimised with warnings as errors, and under
# AddressSanitizer + UBSan (when the compiler has them).
SANITIZE="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
if ! echo 'int main(void){return 0;}' | gcc -x c $SANITIZE - -o "$TMP/sanitizer-probe" 2>/dev/null; then
  echo "note: this compiler has no AddressSanitizer; sanitizer runs skipped"
  SANITIZE=""
fi

# A sanitizer run is quiet when it passes and shows everything when not.
run_quietly() {
  local log="$1"
  shift
  if ! "$@" > "$log" 2>&1; then
    cat "$log"
    echo "FAILED under AddressSanitizer/UBSan: $*" >&2
    return 1
  fi
}

run_suite() {
  local name="$1"
  shift
  gcc -std=c11 -Wall -Wextra -Werror -O2 "$@" -o "$TMP/$name"
  "$TMP/$name"
  if [ -n "$SANITIZE" ]; then
    # shellcheck disable=SC2086
    gcc -std=c11 -Wall -Wextra -Werror $SANITIZE "$@" -o "$TMP/$name-asan"
    run_quietly "$TMP/$name-asan.log" "$TMP/$name-asan"
  fi
}

run_suite core src/core.c tests/core_tests.c
run_suite spell src/core.c src/spell.c tests/spell_tests.c
run_suite typing src/core.c src/typing.c tests/typing_tests.c
run_suite memory src/core.c src/spell.c src/domain.c src/memory.c tests/memory_tests.c
run_suite corpus src/core.c src/domain.c tests/corpus_tests.c
run_suite language-packs src/core.c tests/language_pack_tests.c

# The pack builder (Python) and the app (C) must normalise words alike.
gcc -std=c11 -Wall -Wextra -Werror -O2 src/core.c tests/language_rules.c -o "$TMP/language-rules"
python3 tests/verify_language_rules.py "$TMP/language-rules"
if [ -n "$SANITIZE" ]; then
  # shellcheck disable=SC2086
  gcc -std=c11 -Wall -Wextra -Werror $SANITIZE src/core.c tests/language_rules.c -o "$TMP/language-rules-asan"
  run_quietly "$TMP/language-rules-asan.log" python3 tests/verify_language_rules.py "$TMP/language-rules-asan"
fi

# The application and Setup, type-checked and run on Linux against the
# Win32 subset in tests/win32sim: the hook simulation types words through
# keyboard_hook_proc with simulated keyboards (US, Persian, Russian, French,
# Arabic,
# German) and language packs; the Setup simulation installs packs into an
# in-memory folder.
SIM_FLAGS="-std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Itests/win32sim -Isrc -Iresources -D_WIN32 -DUNICODE -D_UNICODE"
python3 tests/win32sim/generate_weak.py tests/win32sim/windows.h tests/win32sim/oleacc.h > "$TMP/win32-weak.c"
# shellcheck disable=SC2086
gcc $SIM_FLAGS -fsyntax-only src/app.c
# shellcheck disable=SC2086
gcc $SIM_FLAGS -fsyntax-only src/installer.c
sim_suite() {
  local name="$1"
  shift
  # shellcheck disable=SC2086
  gcc $SIM_FLAGS -O1 "$@" -x c "$TMP/win32-weak.c" -o "$TMP/$name"
  "$TMP/$name"
  if [ -n "$SANITIZE" ]; then
    # shellcheck disable=SC2086
    gcc $SIM_FLAGS $SANITIZE "$@" -x c "$TMP/win32-weak.c" -o "$TMP/$name-asan"
    run_quietly "$TMP/$name-asan.log" "$TMP/$name-asan"
  fi
}
sim_suite app-sim tests/app_sim.c src/core.c src/spell.c src/typing.c src/domain.c src/memory.c
sim_suite installer-sim tests/installer_sim.c

# ---- Windows build ----------------------------------------------------------

cd resources
"$ZIG" rc /:auto-includes gnu /c 65001 /fo app.res app.rc
cd ..
"$ZIG" cc -target x86_64-windows-gnu -DUNICODE -D_UNICODE -std=c11 -O2 \
  -Wall -Wextra -Werror -Isrc -Iresources src/app.c src/core.c src/spell.c src/typing.c src/domain.c src/memory.c resources/app.res \
  -o dist/KeySwitchFix.exe -luser32 -lgdi32 -lcomctl32 -lshell32 -ladvapi32 -lole32 -loleaut32 -loleacc -luxtheme -ldwmapi \
  -Wl,/subsystem:windows

cd resources
"$ZIG" rc /:auto-includes gnu /c 65001 /fo uninstaller.res uninstaller.rc
cd ..
"$ZIG" cc -target x86_64-windows-gnu -DUNICODE -D_UNICODE -std=c11 -O2 \
  -Wall -Wextra -Werror -Isrc -Iresources src/installer.c resources/uninstaller.res \
  -o dist/KeySwitchFix-Uninstall.exe -luser32 -lshell32 -ladvapi32 -lole32 -luuid \
  -Wl,/subsystem:windows

cd resources
"$ZIG" rc /:auto-includes gnu /c 65001 /fo installer.res installer.rc
cd ..
"$ZIG" cc -target x86_64-windows-gnu -DUNICODE -D_UNICODE -std=c11 -O2 \
  -Wall -Wextra -Werror -Isrc -Iresources src/installer.c resources/installer.res \
  -o dist/KeySwitchFix-Setup.exe -luser32 -lshell32 -ladvapi32 -lole32 -luuid \
  -Wl,/subsystem:windows

python3 tests/verify_pe.py
cd dist
sha256sum KeySwitchFix-Setup.exe KeySwitchFix-Uninstall.exe KeySwitchFix.exe > SHA256SUMS.txt
if ls languages/*.kslang > /dev/null 2>&1; then
  sha256sum languages/*.kslang >> SHA256SUMS.txt
fi
cat SHA256SUMS.txt
