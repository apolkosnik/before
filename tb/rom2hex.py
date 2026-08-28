#!/usr/bin/env python3
"""Convert a binary ROM image to $readmemh format, one big-endian 16-bit
word per line (even file offset = bits [15:8])."""
import sys

def main():
    if len(sys.argv) != 3:
        sys.exit("usage: rom2hex.py <in.bin> <out.hex>")
    data = open(sys.argv[1], "rb").read()
    if len(data) % 2:
        data += b"\x00"
    with open(sys.argv[2], "w") as f:
        for i in range(0, len(data), 2):
            f.write("%02x%02x\n" % (data[i], data[i+1]))

if __name__ == "__main__":
    main()
