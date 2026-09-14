# OPENSTEP 4.2 WindowServer bitfield fault

WindowServer exits with SIGSEGV in `_os_allocHeapBlock+0x3a` (PC
`0x000236d6`) when AP040 reads past a valid heap boundary tag. The instruction
`bftst 3(a1){6:2}` needs one byte at `0x001e9fff`; the following 8 KiB page at
`0x001ea000` is unmapped. The old memory-bitfield path calculates the span
correctly but always starts with a longword read, which crosses that boundary.

The live exit frame confirms the erroneous longword access: `A1=0x001e9ffc`,
fault address `0x001e9fff`, format/vector `0x7008`, and SSW `0x0d01` (split
access, ATC fault, longword read, user data). The heap metadata and page tables
are consistent. `loginwindow` reports a missing WindowServer port because
WindowServer exits before registering it.

AP040 now follows Previous's `get_bitfield()` / `x_get_bitfield()` access
sizes: byte for span 1, word for span 2, word plus byte for span 3, longword
for span 4, and longword plus byte for span 5. Byte and word results are
left-aligned into the existing bitfield work window. Extraction, modification,
and span-specific stores use the existing execution path.

This shared read path serves all eight memory bitfield instructions: `BFTST`,
`BFEXTU`, `BFEXTS`, `BFFFO`, `BFCHG`, `BFCLR`, `BFSET`, and `BFINS`. The
correction applies to each, including reads preceding a modification. Register
bitfields use a separate path and are unaffected.

`rtl/AP68040/tb/asm/t_bitfield_mmu.s` reproduces the exact user-mode BFTST
with the following page nonresident. It also checks all five spans, signed
dynamic offsets, condition codes, extraction, preservation of surrounding
bits, and genuine crossing faults followed by repair and instruction restart.
The old RTL fails the zero-access-error assertion in all three bus-timing
phases; the corrected RTL passes all 31 checks in all three phases.

Validation commands:

```sh
cd rtl/AP68040/tb
VASM=/opt/amiga-cc/vbcc/bin/vasmm68k_mot ./run_tests.sh
cd ../../../tb
./run_tests.sh
```

Both the AP68040 suite and the NeXT integration suite pass. The regression is
included in both AP68040 test-program build scripts. Live RAM captures, exit
traces, exact binary symbols, and the original investigation are kept locally
under `tb/build/live_20260913_windowserver_2002/`.

The 2026-09-13 RBF build completed with all timing slacks nonnegative (worst
setup +0.287 ns, hold +0.249 ns). `output_files/NeXT.rbf` is 4,442,964 bytes,
SHA-256 `a58df7042d16b200b6e70380b0e7748091755cba78eb32d0fcd6250ac9cc7030`.
Booting OPENSTEP with this new bitstream remains the hardware validation step.
