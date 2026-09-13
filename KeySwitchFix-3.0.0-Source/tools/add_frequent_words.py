#!/usr/bin/env python3
"""OR extra words into an existing KSWB Bloom file (identical to regenerating the union)."""
import struct
import sys
from pathlib import Path


def hash_positions(value: str, bit_mask: int, hash_count: int):
    first = 2166136261
    second = 5381
    for byte in value.encode("utf-8"):
        first = ((first ^ byte) * 16777619) & 0xFFFFFFFF
        second = (((second << 5) + second) ^ byte) & 0xFFFFFFFF
    second = ((second << 1) | 1) & 0xFFFFFFFF
    for index in range(hash_count):
        yield (first + index * second + index * index * 0x9E3779B9) & bit_mask


def main():
    path = Path(sys.argv[1])
    words = sys.argv[2:]
    data = bytearray(path.read_bytes())
    magic, version, bit_count, hash_count = struct.unpack("<4sIII", data[:16])
    assert magic == b"KSWB" and version == 1
    mask = bit_count - 1
    added = 0
    for word in words:
        for bit in hash_positions(word, mask, hash_count):
            byte_index = 16 + (bit >> 3)
            if not data[byte_index] & (1 << (bit & 7)):
                data[byte_index] |= 1 << (bit & 7)
                added += 1
    path.write_bytes(bytes(data))
    print(f"{path.name}: {len(words)} words, {added} new bits")


if __name__ == "__main__":
    main()
