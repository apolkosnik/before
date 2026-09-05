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
    args = parser.parse_args()
    data = args.kernel.read_bytes()
    # The assembly harness uses entry points in this specific release only.
    expected = "f1c68dcb7e99e71c7ada5b1ca733b238b90ed337e8fb9512161e2a7120090ddb"
    if hashlib.sha256(data).hexdigest() != expected:
        parser.error("requires the NeXT Mach 3.3 RELEASE_M68K sdmach used in the panic audit")
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
