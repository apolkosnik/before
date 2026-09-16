# Non-Turbo NeXT FPU frame compatibility

The NeXT integration selects `AP040_FPU_REVISION=8'h40`, matching Previous's
non-Turbo NEXT_CUBE040. The generic AP68040 core and TG68K adapter continue to
default to `8'h41`; other integrations need not change.

## Why this matters

Improv's Mach 2.0 mk-94 F-line handler has a fixed `ADDA.W #0x2c,SP` at
`0x04080322`. With a 52-byte revision-0x41 FSAVE frame, eight bytes of operand
data remain ahead of the real exception frame. Its next instruction tests
that data as the saved SR trace bit. For `FSIN(1.0)` with user tracing off,
it reads `0x8000` and enters vector 9 through software, which can produce
SIGTRAP without a CPU-generated trace event.

The live grey-screen investigation found WindowServer reaped with SIGTRAP,
a running kernel, and an unhandled trace with no retained hardware trace
frame. The exact original WindowServer instruction was no longer recoverable.
The mismatch and software-trace mechanism were reproduced separately with
the exact kernel, and existed before the MLAB FPU register-file import.

## Formats

Offsets are bytes from the FSAVE frame header. BUSY payload offsets remain
unchanged; revision-0x40 UNIMP omits two words present in revision 0x41.

| Field | Revision 0x40 | Revision 0x41 |
| --- | --- | --- |
| IDLE header / total bytes | `40000000` / 4 | `41000000` / 4 |
| UNIMP header / total bytes | `40280000` / 44 | `41300000` / 52 |
| UNIMP CMDREG3B / reserved | absent | +4 / +8 |
| UNIMP STAG / CMDREG1B | +4 / +8 | +12 / +16 |
| UNIMP DTAG / flags | +12 / +16 | +20 / +24 |
| UNIMP FPTEMP / ETEMP | +20 / +32 | +28 / +40 |
| BUSY header / total bytes | `40600000` / 100 | `41600000` / 100 |

FSAVE predecrement, payload serialization, FRESTORE decoding and postincrement
all use the selected format. A short UNIMP restore clears the absent CMDREG3B
field rather than retaining it from an earlier BUSY frame. NULL behavior and
the FPU arithmetic/register-file implementation are unchanged. Non-null
foreign-revision or malformed headers raise format error.

Reference implementation: `reference/previous/src/m68000.c` chooses revision
0x40 for non-Turbo and 0x41 for Turbo; `reference/previous/src/cpu/fpp.c`
sizes the FSAVE frame accordingly (`fpu_version >= 0x41 ? 0x34 : 0x2c`) and
checks the revision in FRESTORE.

Restoring UNIMP **contents** has no reference implementation behind it.
Previous and WinUAE both decode the frame size and then skip the payload
(`// TODO: restore frame contents`, fpp.c:2712), so the field-by-field
restore above is validated only by the directed frame tests and by running
the two real kernels' FPSP handlers, not by agreement with either emulator.
Motorola's FPSP is the primary source for the layout; see also the BUSY
resume path, where WinUAE does implement the frame and agrees field for
field.

## Regression commands

```sh
VASM=/opt/amiga/bin/vasmm68k_mot sh rtl/AP68040/tb/run_fpu_frames.sh
sh tb/run_fpu_revision_tests.sh /absolute/path/to/sdmach /absolute/path/to/odmach
```

The kernels are externally supplied; no guest binaries are added to Git.
The fixture loader checks exact SHA256 identities before use:

- NeXT Mach 3.3 `sdmach`:
  `f1c68dcb7e99e71c7ada5b1ca733b238b90ed337e8fb9512161e2a7120090ddb`.
- Improv Mach 2.0 mk-94 `odmach`:
  `bbfcbb6a92851a45b1c37ea8804945bc3ce6d4b6989cc375619b030cda5bdb16`.

The matrix runs the existing NeXT 3.3 FPSP battery at both revisions, plus
Improv's FSIN(0), FINTRZ, FINT, FSIN(1) and packed-output cases at revision
0x40. Each runs supervisor/user mode, latency 0/3 and stack fill 0000/a55a.
It also checks the original user-mode FSIN(1) incompatibility: revision 0x40
must succeed and revision 0x41 must fail, across both latencies/fills. Before
BUSY-command resumption, the incompatible case reached the software-trace
branch. With resumption implemented, it instead raises a format error at
`0408030a` after the nested unsupported-operand handler; the negative control
checks that precise path. Successful cases must preserve ISP as
well as the active stack and arithmetic results.

Individual profile runs are available through `tb/run_next_fpsp.sh`. Both
profiles default to revision 64 (0x40), the revision this integration builds
and the one a non-Turbo cube reports; `FPU_REVISION=65` selects 0x41. The
matrix above sets the revision itself and covers both regardless.

## Remaining limitations

Expanding the Improv battery to packed-decimal **input** exposed another
failure: its helper at `0x040862f0` repeatedly raises unsupported-data vector
55 on an unnormal extended operand, then fails stage 3. Further tracing
shows 16 successive digit-loop iterations, not unbounded recursive faults.
The handler normalizes the operand and requests BUSY-frame execution with
`CU_SAVEPC=0xfe`. This was ignored when the frame-revision RBF was built.
The working tree now executes these requests, fixing the exact libc zero
parsing regression, but the old kernel's nonzero packed-input case still
fails stage 3 with an incorrectly prepared operand. See
[the resume implementation](FPU_BUSY_RESUME.md) and
[the historical audit](FPU_RESTORE_AUDIT.md). Nonzero packed input remains an explicit opt-in
reproducer, not part of the passing Improv frame-layout subset:

```sh
CHECK_IMPROV_PACKED=1 sh tb/run_next_fpsp.sh /absolute/path/to/odmach improv
```

NeXT 3.3's existing separate pending-dyadic-replay probe also remains opt-in
through `CHECK_REPLAY=1`. Passing these directed tests does not establish a
complete desktop boot. `releases/NeXT_20260915_fpu_rev40_diag.rbf` contains
the frame-revision fix and passed full ROM POST and FPGA timing. It does
**not** contain the subsequent BUSY-resume fix. The later
`NeXT_20260915_cpu_mmu_resume_diag.rbf` contains BUSY resume and passes
timing but has not been deployed. The still later
[ET15 correction](FPU_PACKED_FPSP.md) is in the working tree only and needs
a new build. The older release RBFs do not contain the frame-revision fix.

A packed-only replay (skipping the earlier UNIMP operations) reproduces the
Improv packed-input failure with both 0x40 and 0x41. It is not introduced by
the short-frame selection.
