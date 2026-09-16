#!/usr/bin/env python3
"""Read-only validation of the image-specific diagnostic probe instructions."""
import argparse
import hashlib
import struct
from pathlib import Path

PROBES = {
    0x040573BC: "42a742a7487800062f390409878c61fffffc17c8",
    0x04056B94: "61fffffc1ffe",
    0x04007D6C: "2f0261ffffffcec0",
    0x04002126: "204fd0ef0042216f00460046316f004400444268004a2f48003c4cd7ffffdefc00444e73",
    0x04001852: "3f3c0004426748e7ffff4e7a88002f48003c",
    0x0405732C: "286e0008",
}


def validate(data):
    if len(data) < 28 or data[:4] != b"\xfe\xed\xfa\xce" or struct.unpack_from(">I", data, 4)[0] != 6:
        raise ValueError("expected big-endian m68k Mach-O kernel")
    segments = []
    p = 28
    for _ in range(struct.unpack_from(">I", data, 16)[0]):
        cmd, size = struct.unpack_from(">II", data, p)
        if size < 8 or p + size > len(data):
            raise ValueError("invalid Mach-O load command")
        if cmd == 1:
            segments.append(struct.unpack_from(">4I", data, p+24))
        p += size
    for address, hexdata in PROBES.items():
        expected = bytes.fromhex(hexdata)
        found = None
        for va, size, offset, length in segments:
            if va <= address and address + len(expected) <= va + length:
                found = data[offset+address-va:offset+address-va+len(expected)]
                break
        if found != expected:
            raise ValueError(f"kernel does not match probe sequence at 0x{address:08x}")
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", type=Path)
    args = parser.parse_args()
    try:
        digest = validate(args.kernel.read_bytes())
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"{error}\n")
    print(f"PASS: all {len(PROBES)} Mach 2.0 mk-94 probe sequences match")
    print(f"SHA256 {digest}  {args.kernel}")


if __name__ == "__main__":
    main()
