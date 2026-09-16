# Packed-decimal isolation and ET15 restore fix

The CPU/FPU-only change is implemented and simulation-tested, uncommitted.
No new RBF, deployment, original-kernel modification or disk-image write.
The user explicitly chose not to prepare patched kernel copies.

## RTL defect: ordinary operands were mistaken for underflow

The BUSY restore path treated every set ETE15/FPTE15 flag as a wrapped
negative biased exponent. Motorola FPSP also sets these flags on ordinary
operands with an exponent below `$4000`. For example, its converted `0.1`
has exponent `$3ffb` and ETE15=1. Previously that operand was shifted away
completely, so a correctly prepared `1.0 + 0.1` resumed as `1.0 + 0`.

The fix in `ap040_fpu.v` requires both the extension flag and exponent bit
14 before applying the negative-exponent conversion. It applies to both
frame operands. The existing truncating shift, signed-zero handling and
true negative-exponent cases remain unchanged. No native packed-decimal
instruction or guest-specific instruction/address workaround was added.

This corrects a behavior shared with the local WinUAE/Previous reference:
their `floatx80_denormalize` tests the extension flag alone. Matching the
reference was therefore insufficient to validate the restore protocol.

Primary source cross-checks:

- Motorola FPSP `get_op.asm`, `mk_norm` and `fix_stag`, in the user's local
  `Downloads/os-source/v40_src/workbench/libs/68040` tree. The corresponding
  [published FPSP source](https://android.googlesource.com/kernel/msm.git/%2B/android-msm-swordfish-3.18-nougat-wear-release/arch/m68k/fpsp040/get_op.S)
  sets the flag for normalized exponents at or below `$3fff`.
- The same local tree's `bugfix.asm` explicitly sets ETE15 opposite ETE14
  when reconstructing an ordinary register operand in a BUSY frame.
- MC68040UM section 9.4.1 describes the internal extended exponent; the
  FPSP sources above provide the software-visible restore preparation.

## The original two failures also contain guest defects

These are still failing with the **unchanged kernels**, not counted as
passing regressions:

| Reproducer | Isolated cause outside the RTL |
| --- | --- |
| Improv nonzero packed round-trip | Its digit loop at `040862f0` encodes an extended FMUL immediate, but the following 12 bytes are `40240000 00000000 000281a8`, not extended 10.0. |
| NeXT 3.3 packed `1.0 + 0.1` into FP1 | `decbin` uses FP1 as its `10^17` scaling temporary. Its caller preserves only FP0, then copies the clobbered FP1 into the destination field of the resume frame. |

A bounded CPU-only harness linked the existing **unmodified reference CPU
and FPU objects** from `reference/previous`. It runs the same assembly and
hash-checked kernel fixtures, without the emulator main, GUI, disks or
event loop. Both original failures reproduce there with the same numeric
results. The harness exports two initialization helpers in a private object
copy; it does not replace instruction or FPU implementations.

Before the user's CPU/FPU-only decision, temporary derived hex fixtures
were used as counterfactuals, not deployable kernel copies:

- Replacing Improv's bad immediate with extended 10.0 makes its nonzero
  packed round-trip pass on both reference CPU and RTL.
- Preserving FP1 around NeXT's conversion corrects its FPTEMP. With the old
  ET15 behavior, the answer is still 1.0 on both implementations. Combined
  with the RTL ET15 fix, the result is the expected double
  `3ff19999 9999999a` (1.1).

No guest patch is installed, exposed as a production option, or silently
applied by the normal fixture loader. These counterfactuals establish the
causes; they do not claim the unchanged guest cases are fixed. Producing
their intended results on the unchanged kernels would require a further
compatibility workaround, not simply restoring the supplied operands.

## Repeatable regressions

```sh
VASM=/opt/amiga/bin/vasmm68k_mot sh rtl/AP68040/tb/run_fpu_frames.sh
sh tb/run_fpu_revision_tests.sh /absolute/path/to/sdmach /absolute/path/to/odmach
```

`t_fpu_resume.s` now has 28 numbered checks, including ordinary positive
and negative fractional operands with ETE15/FPTE15 set. Before the fix,
check 25 fails with zero instead of 1.5. Both revisions and all three bus
phases pass after the fix, including the existing true-underflow checks.

The standard NeXT 3.3 fixture adds stage 13: packed `1.0 + 0.1` into FP4,
which the FPSP conversion does not clobber. This uses the **unmodified**
kernel and fails with the old RTL despite correct frame operands. With the
fix it passes at both FPU revisions, supervisor/user mode, latency 0/3 and
stack fill 0000/a55a. The original FP1 case stays separately selectable
with `CHECK_REPLAY=1`; it has not been replaced by the FP4 test.

The full AP68040 suite, prior 288 WinUAE-backed arithmetic vectors, exact
Improv libc zero-parsing regression, and all 26 NeXT integration tests also
pass. Hardware behavior and
FPGA fit/timing for this new change have not been tested. In particular,
`NeXT_20260915_cpu_mmu_resume_diag.rbf` predates this ET15 correction.

Evidence and diagnostic harness:
`tb/build/packed_fpsp_fix_OL7R2E/RESULT.md`.
