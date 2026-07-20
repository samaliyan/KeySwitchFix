#!/usr/bin/env python3
"""Keep user-visible and binary version metadata consistent."""

import re
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

    for path in ("resources/app.rc", "resources/installer.rc", "resources/uninstaller.rc"):
        require(path, f"FILEVERSION {NUMERIC}")
        require(path, f'VALUE "FileVersion", "{VERSION}"')

    app = read("src/app.c")
    assert not re.search(r"\bF12\b|VK_F12|self-test", app, re.IGNORECASE), (
        "src/app.c: obsolete shortcut or self-test UI returned"
    )
    assert read("README.md").startswith("<div align=\"center\">")
    print(f"Metadata verification passed for KeySwitchFix {VERSION}.")


if __name__ == "__main__":
    main()

