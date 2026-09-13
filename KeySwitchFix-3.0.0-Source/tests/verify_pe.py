#!/usr/bin/env python3
"""Verify Windows PE shape and exact embedded installer payloads."""

import hashlib
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PE:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        assert self.data[:2] == b"MZ", f"{path}: missing MZ header"
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        assert self.data[pe:pe + 4] == b"PE\0\0", f"{path}: missing PE header"
        file_header = pe + 4
        self.machine, self.section_count = struct.unpack_from("<HH", self.data, file_header)
        optional_size = struct.unpack_from("<H", self.data, file_header + 16)[0]
        optional = file_header + 20
        magic = struct.unpack_from("<H", self.data, optional)[0]
        assert magic == 0x20B, f"{path}: expected PE32+"
        self.subsystem = struct.unpack_from("<H", self.data, optional + 68)[0]
        directories = optional + 112
        self.resource_rva, self.resource_size = struct.unpack_from("<II", self.data, directories + 16)
        sections = optional + optional_size
        self.sections = []
        for index in range(self.section_count):
            offset = sections + index * 40
            name = self.data[offset:offset + 8].rstrip(b"\0")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", self.data, offset + 8
            )
            self.sections.append((name, virtual_address, max(virtual_size, raw_size), raw_offset))

    def rva_offset(self, rva: int) -> int:
        for _, address, size, raw in self.sections:
            if address <= rva < address + size:
                return raw + (rva - address)
        raise AssertionError(f"{self.path}: RVA {rva:x} is outside sections")

    def resource(self, type_id: int, name_id: int) -> bytes:
        root = self.rva_offset(self.resource_rva)

        def entries(directory_relative: int):
            directory = root + directory_relative
            named, ids = struct.unpack_from("<HH", self.data, directory + 12)
            for index in range(named + ids):
                name, child = struct.unpack_from("<II", self.data, directory + 16 + index * 8)
                yield name & 0xFFFF, child

        type_child = next(child for ident, child in entries(0) if ident == type_id)
        assert type_child & 0x80000000
        name_child = next(child for ident, child in entries(type_child & 0x7FFFFFFF) if ident == name_id)
        assert name_child & 0x80000000
        language_child = next(child for _, child in entries(name_child & 0x7FFFFFFF))
        assert not language_child & 0x80000000
        data_rva, size = struct.unpack_from("<II", self.data, root + language_child)
        start = self.rva_offset(data_rva)
        return self.data[start:start + size]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main():
    app_path = ROOT / "dist" / "KeySwitchFix.exe"
    uninstall_path = ROOT / "dist" / "KeySwitchFix-Uninstall.exe"
    setup_path = ROOT / "dist" / "KeySwitchFix-Setup.exe"
    app = PE(app_path)
    uninstall = PE(uninstall_path)
    setup = PE(setup_path)

    for pe in (app, uninstall, setup):
        assert pe.machine == 0x8664, f"{pe.path}: expected x64 machine"
        assert pe.subsystem == 2, f"{pe.path}: expected Windows GUI subsystem"

    assert digest(app.resource(10, 201)) == digest((ROOT / "resources" / "en.bloom").read_bytes())
    assert digest(app.resource(10, 202)) == digest((ROOT / "resources" / "fa.bloom").read_bytes())
    assert digest(app.resource(10, 203)) == digest(
        (ROOT / "resources" / "en-prefix.bloom").read_bytes()
    )
    assert digest(app.resource(10, 204)) == digest(
        (ROOT / "resources" / "fa-prefix.bloom").read_bytes()
    )
    assert digest(app.resource(10, 205)) == digest(
        (ROOT / "resources" / "en-common.bloom").read_bytes()
    )
    assert digest(app.resource(10, 206)) == digest(
        (ROOT / "resources" / "fa-common.bloom").read_bytes()
    )
    assert digest(app.resource(10, 207)) == digest(
        (ROOT / "resources" / "en-frequent.bloom").read_bytes()
    )
    assert digest(app.resource(10, 208)) == digest(
        (ROOT / "resources" / "fa-frequent.bloom").read_bytes()
    )
    assert digest(app.resource(10, 209)) == digest(
        (ROOT / "resources" / "en-common-prefix.bloom").read_bytes()
    )
    assert digest(app.resource(10, 210)) == digest(
        (ROOT / "resources" / "fa-common-prefix.bloom").read_bytes()
    )
    assert digest(app.resource(10, 211)) == digest(
        (ROOT / "resources" / "en-rank.bin").read_bytes()
    )
    assert digest(app.resource(10, 212)) == digest(
        (ROOT / "resources" / "fa-rank.bin").read_bytes()
    )
    assert digest(setup.resource(10, 301)) == digest(app_path.read_bytes())
    assert digest(setup.resource(10, 302)) == digest(uninstall_path.read_bytes())
    print("PE verification passed: x64 GUI files and all embedded payloads are exact.")


if __name__ == "__main__":
    main()
