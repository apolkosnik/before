# MOVEM restart and failed-translation fixes

Implemented in the uncommitted working tree. The timing-gated diagnostic
RBF `releases/NeXT_20260915_cpu_mmu_resume_diag.rbf` contains these changes.
It has not been deployed; this is not a successful desktop-boot result.
Build evidence: `tb/build/rbf_cpu_mmu_resume_6dpNsP/BUILD.md`.

## MOVEM saved effective address

Register deferral alone did not protect memory-indirect effective addresses.
`MOVEM.L D1-D3,([A0])` with A0=6ff8 and [6ff8]=6ff8 can overwrite its own
pointer before its third store faults at 7000. Recomputing EA after RTE then
writes the wrong destination.

The core now sets format-7 SSW.CM on MOVEM operand faults and stores the
original EA in the frame. RTE reads CM/EA and resumes indexed/PC-relative
MOVEM using that address. Extension words are consumed, but a memory-indirect
pointer is not reread. All transfers replay, as specified. Nested handlers
cannot replace the saved address because it comes from the exception frame.
CM returns into the interrupted instruction; interrupt/trace sampling waits
until that instruction completes. Existing base/index deferral is retained.

`t_movem_restart.s` checks nine cases under all three bus-handshake phases:

- Overwritten pointer; repeated operand fault; nested handler fault.
- Handler-edited EA; explicitly cleared CM (recalculation control).
- Full-extension base/outer displacements; pointer-read fault with CM clear.
- Pending interrupt after continuation; resumed-instruction fetch refault.

The earlier `t_mmu.s` register-index regression remains in the suite.
WB3 stays invalid under the restart model; CT and WB2/WB1 are not added.

## Failed translations stay in the ATC

Each ATC payload now has a resident bit in addition to validity. Invalid
descriptors and table-walk bus errors create valid, nonresident entries.
Hits fault without another search. Protection-denied valid translations
retain their resident bit and protection attributes. PTEST bus errors return
MMUSR.B while installing the nonresident entry. Invalidation, replacement,
and warm-reset retention apply to these entries normally.

`t_atcprobe.s` checks ordinary invalid-page faults, PTEST invalid/bus-error
fills, write-protection retention, instruction/data-bank isolation, 8K pages,
and PTEST replacement. Its handler uses PFLUSHN; descriptor repair alone is
deliberately insufficient. The warm-reset bench checks resident and
nonresident payload retention with the new 46-bit entry layout.

## Validation and references

Run `VASM=/opt/amiga/bin/vasmm68k_mot sh rtl/AP68040/tb/run_tests.sh`.
The complete AP68040 suite passes, including both new programs, cache-snoop
positive/negative controls, and FPU frame/resume tests at revisions 40/41.
A declaration-order correction in the cache-snoop bench removes the old
Icarus compilation blocker; no cache RTL change was needed.

Both new programs fail against the saved pre-fix CPU/MMU, under all three
bus phases. Exact Improv and NeXT 3.3 FPSP baseline matrices pass (eight
configurations each), as does Improv libc `sscanf("0", "%f")` at four
latency/stack-fill combinations. All 26 NeXT device/short-boot/recording
checks pass with exception diagnostics enabled. Nonzero packed failures
remain separate; see [the FPU report](FPU_BUSY_RESUME.md#remaining-limitations).

Primary references supplied locally by the user:

- `MC68040UM_up.pdf`, pp. 8-25 and 8-27: CM/saved-EA restart, repeated
  operand accesses; p. 3-14: nonresident ATC entries.
- WinUAE revision `5db572091715327ec6bcf784d43b6021495d25a9`,
  `cpummu.cpp:452,781,923,1620` and `gencpu.cpp:917`: MOVEM continuation
  and failed-search fill/hit behavior.

Logs, pre-fix sources, reproducer listings and hashes:
`tb/build/cpu_remaining_fix_I50MjQ/RESULT.md`.
Quartus fit/timing and a hardware boot remain untested for these changes.
