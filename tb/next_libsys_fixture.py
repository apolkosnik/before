#!/usr/bin/env python3
"""Load the exact external Improv libc for the FPSP sscanf regression."""
import argparse
import hashlib
import struct
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('library', type=Path)
    p.add_argument('output', type=Path)
    args = p.parse_args()
    data = args.library.read_bytes()
    # No guest binary is distributed by this test.
    if hashlib.sha256(data).hexdigest() != 'dc9279c7973760b53b5e27a98cfadd3a22ab5b0f51168d03db8c04b8958990b3':
        p.error('library hash does not match exact Improv fixture')
    memories = [(0x05000000, bytearray(0x100000), 'libtext.hex'),
                (0x04010000, bytearray(0x10000), 'libdata.hex')]
    pos = 28
    for _ in range(struct.unpack_from('>I', data, 16)[0]):
        cmd, size = struct.unpack_from('>II', data, pos)
        if cmd == 1:
            name = data[pos+8:pos+24].rstrip(b'\0')
            if name != b'__LINKEDIT':
                va, vmsize, off, length = struct.unpack_from('>4I', data, pos+24)
                for base, memory, _ in memories:
                    if base <= va and va+vmsize <= base+len(memory):
                        memory[va-base:va-base+length] = data[off:off+length]
                        break
                else:
                    p.error(f'segment {name!r} outside fixture ranges')
        pos += size
    args.output.mkdir(parents=True, exist_ok=True)
    for _, memory, name in memories:
        (args.output/name).write_text(''.join(memory[i:i+2].hex()+'\n'
                                             for i in range(0,len(memory),2)))
    print('libsys SHA256', hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    main()
