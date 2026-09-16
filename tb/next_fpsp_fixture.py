#!/usr/bin/env python3
"""Load Mach-O segments for the fixed-address NeXT FPSP diagnostic bench."""

import argparse
import hashlib
import struct
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--profile", choices=("next33", "improv"), default="next33")
    args = parser.parse_args()
    data = args.kernel.read_bytes()
    # The assembly harness uses entry points in this specific release only.
    expected = {
        "next33": "f1c68dcb7e99e71c7ada5b1ca733b238b90ed337e8fb9512161e2a7120090ddb",
        "improv": "bbfcbb6a92851a45b1c37ea8804945bc3ce6d4b6989cc375619b030cda5bdb16",
    }[args.profile]
    if hashlib.sha256(data).hexdigest() != expected:
        parser.error(f"kernel hash does not match the exact {args.profile} fixture")
    memory = bytearray(0x100000)
    offset = 28
    for _ in range(struct.unpack_from(">I", data, 16)[0]):
        cmd, size = struct.unpack_from(">II", data, offset)
        if cmd == 1 and data[offset + 8:offset + 24].rstrip(b"\0") != b"__PAGEZERO":
            # LC_SEGMENT; BSS remains zero, symbols are not loaded.
            addr, vmsize, fileoff, filesize = struct.unpack_from(">4I", data, offset + 24)
            start = addr - 0x04000000
            if not 0 <= start <= start + vmsize <= len(memory):
                parser.error("segment outside diagnostic memory aperture")
            memory[start:start + filesize] = data[fileoff:fileoff + filesize]
        offset += size
    with args.output.open("w") as out:
        for offset in range(0, len(memory), 2):
            out.write(memory[offset:offset + 2].hex() + "\n")


if __name__ == "__main__":
    main()
