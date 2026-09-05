#!/usr/bin/env python3
"""Read-only symbol/section audit of a NeXT m68k Mach-O kernel and RAM dump."""

import argparse
import struct
from pathlib import Path


def audit(kernel_path, ram_path):
    kernel = kernel_path.read_bytes()
    magic, cpu, _, _, count, cmd_size, _ = struct.unpack_from(">7I", kernel)
    if magic != 0xFEEDFACE or cpu != 6:
        raise ValueError("expected a big-endian m68k Mach-O executable")
    if 28 + cmd_size > len(kernel):
        raise ValueError("truncated load commands")

    symbols = {}
    sections = []
    entry = None
    offset = 28
    for _ in range(count):
        cmd, size = struct.unpack_from(">2I", kernel, offset)
        if size < 8 or offset + size > 28 + cmd_size:
            raise ValueError("invalid load command size")
        if cmd == 1:  # LC_SEGMENT
            nsects = struct.unpack_from(">I", kernel, offset + 48)[0]
            for index in range(nsects):
                pos = offset + 56 + index * 68
                name = kernel[pos:pos + 16].rstrip(b"\0").decode()
                addr, length = struct.unpack_from(">2I", kernel, pos + 32)
                sections.append((addr, addr + length, name))
        elif cmd == 2:  # LC_SYMTAB
            symoff, nsyms, stroff, strsize = struct.unpack_from(">4I", kernel, offset + 8)
            strings = kernel[stroff:stroff + strsize]
            for index in range(nsyms):
                strx, kind, _, _, value = struct.unpack_from(">IBBHI", kernel, symoff + 12 * index)
                if kind & 0xE0 or kind & 0x0E != 0x0E:  # only N_SECT, not STABs
                    continue
                end = strings.find(b"\0", strx)
                if end < strx:
                    raise ValueError("unterminated symbol name")
                symbols[strings[strx:end].decode()] = value
        elif cmd == 5:  # LC_UNIXTHREAD, M68K_THREAD_STATE_REGS
            flavor, words = struct.unpack_from(">2I", kernel, offset + 8)
            if flavor == 1 and words == 18:
                entry = struct.unpack_from(">I", kernel, offset + 16 + 17 * 4)[0]
        offset += size

    if entry is not None:
        print(f"Kernel entry: {entry:08x}")
    for start, end, name in sections:
        print(f"Section {name:12s}: {start:08x}..{end - 1:08x}")

    start = symbols.get("_init_TEXT_BEGIN")
    end = symbols.get("_init_TEXT_END")
    if start is not None and end is not None:
        print(f"Initialization text symbols: {start:08x}..{end:08x} (end exclusive)")
        # Alignment performed by the cleanup routine called from _main in NS3.3.
        lo = (start + 31) & ~15
        hi = (end - 15) & ~15
        print(f"NS3.3 cleanup interval: {lo:08x}..{hi - 1:08x}, {hi - lo} bytes")
        print("This interval is reclaimable initialization storage, not permanent text.")

    for name, value in sorted(symbols.items(), key=lambda item: item[1]):
        if value == 0x0406BE48 or name in ("_main", "_dbg_trap", "_client_pcb"):
            print(f"Symbol {name}: {value:08x}")

    for address in (0x040B62B0, 0x040CA7B4):
        names = [name for lo, hi, name in sections if lo <= address < hi]
        print(f"Address {address:08x} is in {', '.join(names) or 'no section'}")
    if ram_path is not None:
        ram = ram_path.read_bytes()
        if len(ram) != 64 * 1024 * 1024:
            raise ValueError("expected 64 MiB of guest RAM in ascending guest byte order")
        for address in (0x040B62B0, 0x040CA7B4):
            value = struct.unpack_from(">I", ram, address - 0x04000000)[0]
            print(f"RAM[{address:08x}] = {value:08x}")
        print("These NS3.3 debugger fields record the fault; they are not evidence of its cause.")
        # The captured FPSP exception is above the nested access-error frame.
        # Locate its stacked next-PC/vector, rather than treating SR+PC_hi as
        # a C return address (the kernel backtrace prints 0x00180505).
        signature = bytes.fromhex("0505429a30dc")
        pos = ram.find(signature)
        while pos >= 0:
            frame = pos - 2
            a6 = frame - 4
            if a6 >= 192 and pos % 2 == 0 and frame + 12 <= len(ram):
                sr = struct.unpack_from(">H", ram, frame)[0]
                ea = struct.unpack_from(">I", ram, frame + 8)[0]
                d0 = struct.unpack_from(">I", ram, a6 - 192)[0]
                fp0 = ram[a6 - 176:a6 - 164].hex()
                print(f"NS3.3 packed-store exception frame at {0x04000000 + frame:08x}:")
                print(f"  SR={sr:04x} next-PC=0505429a format/vector=30dc EA={ea:08x}")
                print(f"  FPSP saved D0={d0:08x}, FP0={fp0}")
            pos = ram.find(signature, pos + 1)


def audit_libsys(path):
    """Verify the instruction in the library independently of the RAM dump."""
    data = path.read_bytes()
    magic, cpu, _, _, count, _, _ = struct.unpack_from(">7I", data)
    if magic != 0xFEEDFACE or cpu != 6:
        raise ValueError("expected big-endian m68k Mach-O libsys")
    pos = 28
    for _ in range(count):
        cmd, size = struct.unpack_from(">II", data, pos)
        if cmd == 1:
            addr, _, fileoff, filesize = struct.unpack_from(">4I", data, pos + 24)
            target = 0x05054296
            if addr <= target and target + 4 <= addr + filesize:
                off = fileoff + target - addr
                opcode = data[off:off + 4]
                print(f"libsys[05054296] = {opcode.hex()}")
                if opcode == bytes.fromhex("f2107c00"):
                    print("  FMOVE.P FP0,(A0){D0}: dynamic-k packed-decimal store")
        elif cmd == 2:
            so, ns, st, ss = struct.unpack_from(">4I", data, pos + 8)
            strings = data[st:st + ss]
            for index in range(ns):
                x, kind, _, _, value = struct.unpack_from(">IBBHI", data, so + 12 * index)
                if kind & 0xE0 or kind & 0x0E != 0x0E:
                    continue
                end = strings.find(b"\0", x)
                name = strings[x:end].decode()
                if name == "__dbltopdfp":
                    print(f"Symbol {name}: {value:08x}")
        pos += size


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", type=Path, help="extracted /sdmach executable")
    parser.add_argument("--ram", type=Path, help="optional 64 MiB guest RAM dump")
    parser.add_argument("--libsys", type=Path, help="optional extracted /usr/shlib/libsys_s.B.shlib")
    args = parser.parse_args()
    audit(args.kernel, args.ram)
    if args.libsys is not None:
        audit_libsys(args.libsys)
