# Workspace SIGBUS: BUSY-frame command resumption

Status: implemented and simulation-tested in the uncommitted working tree.
The timing-gated diagnostic RBF
`releases/NeXT_20260915_cpu_mmu_resume_diag.rbf` contains this change but has
not been deployed. Desktop recovery still needs hardware confirmation.
Build evidence: `tb/build/rbf_cpu_mmu_resume_6dpNsP/BUILD.md`.
The later [packed-decimal investigation](FPU_PACKED_FPSP.md) fixes an ET15
restore error in the working tree; that correction is not in this RBF.

## Failure and fix

The live Workspace failure was a SIGBUS at `objc_msgSend+8` (`0500c020`),
dereferencing object pointer `0000000c`. Dock initialization had produced
zero slots and subsequently used slot -1. All four parsed screen bounds
contained the same stale single-precision value `42dd5233` (about 110.66).
The recovered default-value path calls libc `sscanf` with format `%f` and
default string `"0"`. The actual configured strings were not recovered.

The exact Improv libc/kernel pair reproduces this stale-result bug without
running Workspace: `sscanf("0", "%f")` returns one successful conversion,
but previously stored the prior FP0 value instead of zero. Its packed-input
handler prepares a 68040 BUSY frame and requests execution with
`CU_SAVEPC=fe`. The old FRESTORE only installed the frame; it never executed
that command.

The new CPU/FPU path:

- Reads CU_SAVEPC, ET15 and FPTE15 from the 100-byte BUSY frame.
- Resumes supported opclass 0/2 arithmetic using **both frame operands**,
  not the current FP register contents; maps internal SQRT command 5 to 4.
- Handles negative internal exponents through truncating denormalization
  and signed working exponents, using the existing right-shift engine.
- Clears instruction exception status while retaining accrued status,
  FPCR and the software-restored FPIAR. Normal completion/writeback and
  deferred arithmetic-exception handling remain in use.
- Interlocks subsequent FP instructions and FSAVE/FRESTORE until the
  resumed operation finishes. Execution starts only after the frame has
  been completely read.

No guest executable, disk image, boot-device setting or diagnostic capture
logic was changed for this fix. Other resume opclasses and software-only
opcodes are not added by this implementation; this is not a claim of full
68040 internal-frame compatibility.

## Reference validation

Compared with the user's clean local WinUAE revision
`5db572091715327ec6bcf784d43b6021495d25a9`:

- `fpp.cpp:2650-2703`: BUSY-frame fields, resume dispatch and exception check.
- `fpp_softfloat.cpp:283` and `softfloat/softfloat.cpp:3420`: extended
  exponent handling and truncating shifts.

A small diagnostic driver links the **unmodified WinUAE softfloat.cpp**;
no WinUAE GUI/emulator process was launched. Its 288 vectors cover eight
operations, negative-exponent/denormal/zero/infinity operands, shift
boundaries, rounding/precision modes and seeded operand pairs. All vectors
pass under three AP68040 bus-handshake phases. Result comparison preserves
raw bits for nonzero values but canonicalizes finite zero exponents,
retaining their sign, to match AP68040's existing writeback convention.
Instruction exception status is compared too. This is not an exhaustive
IEEE or full WinUAE CPU comparison.

## Repeatable regressions

```sh
VASM=/opt/amiga/bin/vasmm68k_mot sh rtl/AP68040/tb/run_fpu_frames.sh
LIBSYS=/absolute/path/to/libsys_s.B.shlib \
    sh tb/run_next_fpsp.sh /absolute/path/to/odmach improv
sh tb/run_fpu_revision_tests.sh /absolute/path/to/sdmach /absolute/path/to/odmach
EXCEPTION_DIAG=1 sh tb/run_tests.sh
```

The current `t_fpu_resume.s` supplies 28 directed checks plus pointer/stack
guards, at both frame revisions and all three bus phases. It covers frame
versus live-register operands, no-resume controls, extended exponents,
FCMP/FTST, status/FPIAR preservation and deferred divide-by-zero delivery.
The last four checks cover the later ordinary-operand ET15 correction.

The optional libc regression runs four consecutive real `sscanf("0", "%f")`
calls at each latency 0/3 and stack fill 0000/a55a. Every call seeds FP0 with
the observed stale value and checks that parsing replaces it with zero.
Pre-fix RTL fails this same test; new RTL passes all four configurations.
The fixture maps user libc data separately from overlapping kernel text by
function code; caches/MMU remain disabled, so this is not an OS boot test.

Kernels use the identities listed in [FPU_FRAME_REVISION.md](FPU_FRAME_REVISION.md).
The optional libc SHA256 is
`dc9279c7973760b53b5e27a98cfadd3a22ab5b0f51168d03db8c04b8958990b3`.
Fixture loaders reject other binaries. No guest binaries are added to Git.

Additional results: all 24 positive kernel-matrix configurations and all
four paired revision controls pass. Eight CPU program regressions and five
helper benches pass. All 26 NeXT device/short-ROM-boot/recording checks pass.
The aggregate AP68040 runner is blocked by an existing Icarus elaboration
error in `tb_ap040_cache_snoop.v` (`perm_en` declared after use); the eight
CPU programs and five helpers were run separately using fresh binaries.
That cache-snoop bench is not counted as passed.

Follow-up: the [CPU/MMU fixes](CPU_MMU_RESTART.md) correct the bench's
declaration order, and the complete AP68040 runner now passes, including
the cache-snoop controls. Its results supersede that earlier runner limit.

Evidence, oracle sources/vectors and full logs:
`tb/build/frestore_resume_fix_X6ObnW/RESULT.md`.
Earlier live-memory findings:
`tb/build/live_workspace_sigbus_J008sy/findings.md`.

## Remaining limitations

The existing opt-in Improv **nonzero** packed-input case still fails stage 3.
FRESTORE now executes its supplied command, but the exact old kernel prepares
an incorrect value. Its helper contains
`f23c4823 40240000 00000000 000281a8`, which specifies an extended operand
whose bytes do not encode 10.0. No kernel patch has been made. The separate
NeXT 3.3 opt-in dyadic replay also fails, with new operand-provenance evidence
below.
See [the historical audit](FPU_RESTORE_AUDIT.md) for those traces.

The new `+operands` trace follows the NeXT 3.3 stage-6 destination through
the actual kernel helper:

| Instruction boundary | FP1 |
| --- | --- |
| `040031bc`, entering `decbin` | 1.0 |
| `04003454`, returning from `decbin` | 10^17 |
| `04004270`, dynamic FMOVEM into FPTEMP | 10^17 |
| `04003d90`, FRESTORE after user FP-register restoration | 1.0 live, but 10^17 in FPTEMP |

The value `40370000b1a2bc2ec5000000` is exactly 10^17: the helper's decimal
scaling temporary, not random corruption. This path saves/restores FP0
around `decbin`, but the helper also changes FP1; the subsequent dynamic
FMOVEM selects that live FP1. WinUAE `fpp.cpp:2796-2858` likewise stores
live registers for FMOVEM. Its BUSY restore uses FPTEMP, not live FP1.
Thus the bad operand is present before FRESTORE, and substituting live FP1
in RTL would violate the validated resume semantics.

This localizes the failure within the observed kernel/fixture execution;
the [follow-up investigation](FPU_PACKED_FPSP.md) independently reproduces
both original failures on Previous's reference CPU and confirms the guest
defects with temporary counterfactual fixtures. It also isolates and fixes
a separate RTL ET15 interpretation error using an unchanged-kernel FP4
test. Both original nonzero packed regressions still fail with unchanged
kernels; the user chose CPU/FPU-only changes. Earlier operand evidence:
`tb/build/cpu_remaining_fix_I50MjQ/next33_operands.log`.

The existing `NeXT_20260915_fpu_rev40_diag.rbf` does **not** contain this
resume fix. Its earlier timing/full-ROM-POST results do not validate this
new RTL. The newer `NeXT_20260915_cpu_mmu_resume_diag.rbf` contains the fix
and passes the existing timing gate. A hardware boot is still required
before claiming the Workspace error is resolved on MiSTer.
