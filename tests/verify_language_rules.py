#!/usr/bin/env python3
"""The pack builder must normalise words exactly as the app looks them up.

Compares tools/build_language_pack.py (is_letter, to_lower, lookup_form)
with src/core.c through the tests/language_rules helper built by
build-native.sh: every BMP code point, and sample words under every flag set
the builder uses.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_language_pack as pack  # noqa: E402


def fail(message):
    raise SystemExit(f"FAIL: {message}")


def main():
    helper = sys.argv[1] if len(sys.argv) > 1 else "/tmp/keyswitchfix-language-rules"
    table = subprocess.run([helper, "table"], capture_output=True, text=True, check=True).stdout
    for line in table.splitlines():
        code, letter, lower = (int(value) for value in line.split())
        if pack.is_letter(chr(code)) != bool(letter):
            fail(f"is_letter differs at U+{code:04X}")
        if ord(pack.to_lower(chr(code))) != lower:
            fail(f"to_lower differs at U+{code:04X}")
    samples = [
        "Hello", "don't", "l'eau", "'quoted'", "Straße", "ÄRGER", "İstanbul", "ПРИВЕТ", "ёлка",
        "Λόγος", "ΆΈΉ", "שָׁלוֹם", "حتماً", "مدرّس", "café", "naïve", "x1", "a-b", "", "ok'",
        "Ünïcödé", "ŒUVRE", "Ελληνικά", "Українська", "ґанок", "عربي", "ابو",
        "a" * 32, "a" * 33, "ب" * 32 + "\u064e", "ب" * 33 + "\u064e", "A" * 31 + "'",
    ]
    flag_sets = {flags for (_, _, _, flags) in pack.LANGUAGES.values()}
    flag_sets |= {pack.CASED | pack.ASCII_ONLY | pack.APOSTROPHE | pack.LATIN,
                  pack.ARABIC_MARKS | pack.PERSIAN_ALEF | pack.NO_SHAPE}
    for flags in sorted(flag_sets):
        coded = "\n".join(" ".join(f"{ord(c):x}" for c in word) for word in samples) + "\n"
        result = subprocess.run([helper, str(flags)], input=coded,
                                capture_output=True, text=True, check=True).stdout.splitlines()
        if len(result) != len(samples):
            fail(f"helper answered {len(result)} of {len(samples)} words")
        for word, c_coded in zip(samples, result):
            c_form = "-" if c_coded == "-" else "".join(chr(int(v, 16)) for v in c_coded.split())
            py_form = pack.lookup_form(word, flags) if word else None
            if (py_form or "-") != c_form:
                fail(f"lookup form of {word!r} with flags {flags:#x}: C {c_form!r}, Python {py_form!r}")
    print(f"Language rules agree: {len(table.splitlines())} code points, "
          f"{len(samples)} words x {len(flag_sets)} flag sets.")


if __name__ == "__main__":
    main()
