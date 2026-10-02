#!/usr/bin/env python3
"""Verify Windows PE shape and exact embedded installer payloads."""

import hashlib
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check(condition, message: str = "PE verification failed") -> None:
    """A real check: unlike assert, it also runs under python -O."""
    if not condition:
        raise SystemExit(f"FAIL: {message}")


class PE:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        check(self.data[:2] == b"MZ", f"{path}: missing MZ header")
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        check(self.data[pe:pe + 4] == b"PE\0\0", f"{path}: missing PE header")
        file_header = pe + 4
        self.machine, self.section_count = struct.unpack_from("<HH", self.data, file_header)
        optional_size = struct.unpack_from("<H", self.data, file_header + 16)[0]
        optional = file_header + 20
        magic = struct.unpack_from("<H", self.data, optional)[0]
        check(magic == 0x20B, f"{path}: expected PE32+")
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

        type_child = next((child for ident, child in entries(0) if ident == type_id), None)
        check(type_child is not None, f"{self.path}: no resources of type {type_id}")
        check(type_child & 0x80000000, f"{self.path}: resource type {type_id} is not a directory")
        name_child = next((child for ident, child in entries(type_child & 0x7FFFFFFF) if ident == name_id), None)
        check(name_child is not None, f"{self.path}: resource {name_id} is missing")
        check(name_child & 0x80000000, f"{self.path}: resource {name_id} is not a directory")
        language_child = next((child for _, child in entries(name_child & 0x7FFFFFFF)), None)
        check(language_child is not None, f"{self.path}: resource {name_id} is empty")
        check(not language_child & 0x80000000, f"{self.path}: resource {name_id} has no data entry")
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
        check(pe.machine == 0x8664, f"{pe.path}: expected x64 machine")
        check(pe.subsystem == 2, f"{pe.path}: expected Windows GUI subsystem")

    embedded = {
        201: "en.bloom", 202: "fa.bloom", 203: "en-prefix.bloom", 204: "fa-prefix.bloom",
        205: "en-common.bloom", 206: "fa-common.bloom", 207: "en-frequent.bloom",
        208: "fa-frequent.bloom", 209: "en-common-prefix.bloom", 210: "fa-common-prefix.bloom",
        211: "en-rank.bin", 212: "fa-rank.bin",
    }
    for resource_id, name in embedded.items():
        check(digest(app.resource(10, resource_id)) == digest((ROOT / "resources" / name).read_bytes()),
              f"KeySwitchFix.exe: resource {resource_id} is not resources/{name}")
    # Every executable carries the manifest: visual styles (comctl32 v6),
    # per-monitor DPI awareness and no elevation request.
    for pe in (app, uninstall, setup):
        manifest = pe.resource(24, 1)
        for needle in (b"Microsoft.Windows.Common-Controls", b"PerMonitorV2", b'level="asInvoker"'):
            check(needle in manifest, f"{pe.path}: manifest lacks {needle.decode()}")
    check(digest(setup.resource(10, 301)) == digest(app_path.read_bytes()),
          "Setup does not carry this KeySwitchFix.exe")
    check(digest(setup.resource(10, 302)) == digest(uninstall_path.read_bytes()),
          "Setup does not carry this uninstaller")
    bundle = setup.resource(10, 303)
    check(digest(bundle) == digest((ROOT / "dist" / "languages.bundle").read_bytes()),
          "Setup does not carry dist/languages.bundle")
    packs = verify_bundle(bundle)
    print("PE verification passed: x64 GUI files and all embedded payloads are exact; manifests present; "
          f"{packs} language pack(s) in Setup.")


def verify_bundle(bundle: bytes) -> int:
    """Setup's language packs: the same files as dist/languages, each a pack
    whose header names the language its file name says."""
    check(bundle[:4] == b"KSLB", "language bundle: bad magic")
    count = struct.unpack_from("<I", bundle, 4)[0]
    check(count <= 64, f"language bundle: {count} packs, Setup installs at most 64")
    check(8 + 32 * count <= len(bundle), "language bundle: truncated table")
    on_disk = sorted(p.name for p in (ROOT / "dist" / "languages").glob("*.kslang"))
    names = []
    for index in range(count):
        entry = 8 + 32 * index
        name = bundle[entry:entry + 24].split(b"\0", 1)[0].decode("ascii")
        offset, size = struct.unpack_from("<II", bundle, entry + 24)
        data = bundle[offset:offset + size]
        check(len(data) == size and size > 132, f"language bundle: {name} truncated")
        check(data[:4] == b"KSLP", f"language bundle: {name} is not a pack")
        code = data[8:16].split(b"\0", 1)[0].decode("ascii")
        check(name == f"{code}.kslang", f"language bundle: {name} holds {code}")
        check(data == (ROOT / "dist" / "languages" / name).read_bytes(), f"language bundle: {name} differs")
        names.append(name)
    check(sorted(names) == on_disk, "language bundle: not the packs in dist/languages")
    return count


if __name__ == "__main__":
    main()
