# BUSY-frame resume audit

## Current status

The working tree now implements the resume path identified below. The exact
Improv libc `sscanf("0", "%f")` regression fails with the pre-fix RTL and
passes with the new RTL. See [the implementation and validation report](FPU_BUSY_RESUME.md)
for scope, remaining packed-input limitations and pending hardware testing.
The rest of this document records the **pre-fix audit**, not current behavior.

Source-level comparison against `/home/adam/WinUAE` at clean revision
`5db572091715327ec6bcf784d43b6021495d25a9`, plus directed AP68040 simulation.
No WinUAE executable was launched. No CPU/FPU RTL, guest image, RBF, or live
MiSTer state was changed during this audit; nothing was committed.

## Confirmed implementation gap

WinUAE `fpp.cpp:2650-2703` handles a 68040 BUSY frame as follows:

1. Read `CU_SAVEPC` from the high byte of the longword at frame offset +8.
2. Read the extended exponent bits at bit 28 of +0x3c and +0x44, the command
   from +0x40, FPTEMP from +0x4c, and ETEMP from +0x58.
3. If `CU_SAVEPC == 0xfe` and opclass is 0 or 2, reconstruct both operands,
   clear instruction exception status, execute the restored command, write
   its destination when permitted, and check arithmetic exceptions.

The extended exponent handling is delegated to `fp_denormalize` in
`fpp_softfloat.cpp:283` and `floatx80_denormalize` in
`softfloat/softfloat.cpp:3420`. A fix must account for these bits rather
than silently interpreting a negative internal exponent as a huge positive
one. WinUAE itself logs unsupported resume opclasses; this path should not
be treated as a complete specification for every possible BUSY frame.

In AP68040, `S_FREST_B` reads the payload but ignores word index 2
(`CU_SAVEPC`) and the extended exponent bits. `S_FREST_BD` merely installs
the frame state, updates the address register, and fetches the next
instruction. `ap040_fpu.v`'s `frestore_unimp` block stores the state but never
starts the restored operation. Arithmetic exception re-arming is separate
and is not a substitute for command execution.

## Kernel-free reproducer

An explicitly constructed 100-byte BUSY frame contains:

- Header `40600000` or `41600000`, depending on revision.
- `CU_SAVEPC=fe`, command `48a2` (FADD.X to FP1), no pending E1/E3/T bits.
- ETEMP=1.0 and FPTEMP=2.0; the live FP1 initially contains 99.0.

After FRESTORE, FP1 must contain 3.0 according to the WinUAE path. AP68040
leaves it at 99.0. Clearing only CU_SAVEPC provides a control that correctly
preserves 99.0. Both revisions reproduce this across latency 0/3 and stack
fill 0000/a55a: eight expected failures, eight passing controls. Address
postincrement is correctly 100 bytes. This separates the missing execution
from the older 44/52-byte UNIMP-frame issue and from guest software.

Evidence and rerun script:
`tb/build/fpu_packed_audit_68g1t3/run_restore_probe.sh` and its `probe_*.log`
files. This diagnostic script expects the current bug; its exit zero is
not a passing architectural regression. Revision 0x40 uses the freshly
built tracing bench; revision 0x41 reuses the prior matrix executable with
the identical CPU/FPU RTL hashes.

## Exact-kernel traces and remaining uncertainty

`improv_frames.log` corrects the prior recursive-loop diagnosis: the helper
at `040862f0` faults 16 times at the same ISP (`df44`), returns through
FRESTORE/RTE each time, and loops through DBRA at `0408630a`. Each nested
handler supplies a normalized source and `CU_SAVEPC=fe`. The outer packed
FMOVE ultimately supplies command `4880` with the same resume request,
but FP1 remains its reset NaN.

The immediate operand at that helper is also suspicious: the exact kernel
and both existing grey-screen RAM captures contain
`f23c4823 40240000 00000000 000281a8`. The opcode specifies an extended
operand whose value is `20533/8388608`, not 10.0. Its first eight operand
bytes alone encode double-precision 10.0. This observation does not establish
why the kernel contains those bytes or authorize patching them. It prevents
claiming that implementing resume alone will make this whole Improv test
numerically correct.

The NeXT 3.3 `CHECK_REPLAY=1` case also reaches FRESTORE with
`CU_SAVEPC=fe` and leaves FP1 unchanged. Its restored FPTEMP is not the
expected original 1.0, so the destination-operand preparation needs a
separate trace before claiming full packed-dyadic correctness.

`analysis.log` records the static-byte/live-capture comparisons and the
bounded exception-loop checks. `next33_frames.log` records the second
kernel. The shared testbench now accepts `+frames` to report completed
FRESTORE payloads; exception logging includes ISP to distinguish nesting
from repeated calls. The CPU/FPU RTL hashes remain those of the preceding
frame-revision implementation.

## Exception format cross-check

WinUAE `fpp.cpp:3559` explicitly chooses mid/post-instruction delivery for
unsupported operands; `newcpu_common.cpp:1643` builds format 3 with the
following PC on that path. This agrees with our current delivery behavior.
It does not independently establish the hardware behavior of every vector-55
case: the Motorola manual's pre/post distinction and hardware corpus must
still be considered. Changing exception format is not needed to explain
the isolated resume failure above.

Hardware WindowServer recovery and the separate Checking-disks freeze
remain unverified. No RBF was built or deployed during this audit.

Subsequent build: `releases/NeXT_20260915_fpu_rev40_diag.rbf` passes timing
and full ROM POST, but contains only the earlier frame-revision fix, **not**
a fix for BUSY-frame resume. It has not been deployed or hardware-validated.
