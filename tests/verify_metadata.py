#!/usr/bin/env python3
"""Keep user-visible and binary version metadata consistent."""

import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
NUMERIC = VERSION.replace(".", ",") + ",0"


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(path: str, value: str) -> None:
    assert value in read(path), f"{path}: missing {value!r}"


def main() -> None:
    require("src/app.c", f'#define APP_VERSION L"{VERSION}"')
    require("src/installer.c", f'#define APP_VERSION L"{VERSION}"')
    require("src/app.c", f'L"KeySwitchFix {VERSION}"')
    require("src/core.h", "#define KS_MAX_SEQUENCE_WORDS 32")
    require("src/core.h", "#define KS_MAX_SEQUENCE_CHARS 512")
    require("src/core.c", "int ks_is_word_scancode(")
    require("src/app.c", "if (!ks_is_word_scancode(scan_code)) return 0;")
    require("src/core.c", "int ks_evaluate_sequence(")
    require("src/app.c", "try_sequence_correction(")
    require("src/app.c", "store_pending_word(")
    require("src/app.c", "try_undo(1)")

    for path in ("resources/app.rc", "resources/installer.rc", "resources/uninstaller.rc"):
        require(path, f"FILEVERSION {NUMERIC}")
        require(path, f"PRODUCTVERSION {NUMERIC}")
        require(path, f'VALUE "FileVersion", "{VERSION}"')
        require(path, f'VALUE "ProductVersion", "{VERSION}"')

    bloom_shapes = {
        "resources/en.bloom": (1 << 21, 9),
        "resources/fa.bloom": (1 << 22, 9),
        "resources/en-prefix.bloom": (1 << 21, 14),
        "resources/fa-prefix.bloom": (1 << 21, 14),
        "resources/en-common.bloom": (1 << 20, 10),
        "resources/fa-common.bloom": (1 << 20, 10),
        "resources/en-frequent.bloom": (1 << 17, 10),
        "resources/fa-frequent.bloom": (1 << 17, 10),
        "resources/en-common-prefix.bloom": (1 << 20, 14),
        "resources/fa-common-prefix.bloom": (1 << 20, 14),
    }
    for path, (expected_bits, expected_hashes) in bloom_shapes.items():
        data = (ROOT / path).read_bytes()
        magic, version, bit_count, hash_count = struct.unpack("<4sIII", data[:16])
        assert magic == b"KSWB" and version == 1, f"{path}: invalid Bloom header"
        assert bit_count == expected_bits, f"{path}: unexpected bit count"
        assert hash_count == expected_hashes, f"{path}: unexpected hash count"
        assert len(data) == 16 + bit_count // 8, f"{path}: invalid Bloom size"

    core = read("src/core.c")
    generator = read("tools/generate_blooms.py")

    def c_list(function: str) -> set:
        body = core.split(f"static int {function}(", 1)[1].split("};", 1)[0]
        return set(re.findall(r'L"([^"]+)"', body))

    def py_set(name: str) -> set:
        body = generator.split(f"{name} = {{", 1)[1].split("}", 1)[0]
        return set(re.findall(r'"([^"]+)"', body))

    assert c_list("short_english_word") == py_set("SHORT_ENGLISH_WORDS"), (
        "short English word list differs between core.c and generate_blooms.py"
    )
    assert c_list("short_persian_word") == py_set("SHORT_PERSIAN_WORDS"), (
        "short Persian word list differs between core.c and generate_blooms.py"
    )

    app = read("src/app.c")
    assert not re.search(r"\bF12\b|VK_F12|self-test", app, re.IGNORECASE), (
        "src/app.c: obsolete shortcut or self-test UI returned"
    )
    assert read("README.md").startswith("<div align=\"center\">")
    print(f"Metadata verification passed for KeySwitchFix {VERSION}.")


if __name__ == "__main__":
    main()
