# WindowServer unhandled-trap diagnostic build

The exception instrumentation is passive, **not a hang fix**. It targets the Improv
optical image's **NeXT Mach 2.0 mk-94** kernel. Do not use another OS/kernel
and assume an empty mailbox means no failure occurred.

The version-2 RBF captured init's early, nonfatal vector-9 trace. This kernel
also uses user single-stepping for deferred work (AST). Version 3 therefore
waits for actual unhandled-trace/breakpoint delivery, or signal-5 exit.

## Hardware test

Validated RBF: `releases/NeXT_20260915_fpu_rev40_diag.rbf`
(4,484,728 bytes), SHA256
`f27bf5a86bf38da7762abd1689ede49e5ed35ce8331d0bc8a1cf98d9851ddd31`.
Directed tests, full 1.4-billion-cycle POST, and timing gate passed.

This RBF includes the uncommitted revision-0x40 FPU compatibility fix
described in [FPU_FRAME_REVISION.md](FPU_FRAME_REVISION.md). Manual hardware
testing is still needed. The separate BUSY-frame resume gap described in
[FPU_RESTORE_AUDIT.md](FPU_RESTORE_AUDIT.md) is **not fixed** in this build.

This includes the core/FPU changes from ap040-40mhz commit
`0ba8f7c0e29d360ea01f2bc2e8714360c0d4f15b`: MLAB FP registers and the coupled
FMOVEM source-port selection. The version-3 diagnostic is unchanged; it
does not provide live CPU/DMA-state capture. Whether this build changes the
Checking-disks freeze remains a hardware-test question.

1. Load the new diagnostic RBF manually and boot the **same Improv image**.
2. If it stops at Checking disks or the grey screen, leave it running without
   resetting/reloading and report which screen is visible.
3. Collect and decode the mailbox over SSH:

   ```sh
   sh tb/read_exception_mailbox.sh root@mister > tb/build/exception_capture.txt
   python3 tb/decode_exception.py tb/build/exception_capture.txt
   ```

   Leave the machine running for a separate guest-RAM snapshot, needed to
   identify the process and inspect the captured instruction's context.

The reader is read-only. Neither instrumentation nor reader pauses the CPU,
modifies guest memory, or resets/deploys the FPGA. The publisher adds only
low-priority writes in its reserved HPS region. Ethernet may be disabled;
no Main_MiSTer changes are required.

Verify the loaded RBF hash. Diagnostic reset clears the record; a normal
RBF may leave old DDR contents. Repeated headers detect publication during
a read, not boot identity. Clearing the HPS captured flag does **not** rearm
the private FPGA latch. Do not change it; reset/reload is needed for a new boot.

## Trigger and matching

`NEXT_EXCEPTION_DIAG=1` selects CPU diagnostic mode 2 in NeXT.sv. Mode 0
disables diagnostics; mode 1 retains the old version-2 tap for tests.
The instrumentation is **opt-in**: files.qip applies the macro and its
aggressive-routability/area/seed assignments only when the environment
variable is set, so an ordinary `quartus_sh --flow compile NeXT` produces a
normal RBF and needs no edit. No diagnostic register feeds CPU execution
controls.

Internal events are:

- Candidate: user trace (vector 9) or TRAP #15 (vector 47), with original
  stacked PC/SR, opcode/source address, USP, URP, and exception-frame address.
- Retirement: final user RTE, using the pre-pop frame address. The kernel's
  compaction path also retires the original frame **before moving it**.
- Delivery: execution of a matching supervisor instruction below.

| Probe | Address: opcode | Additional guard | Meaning |
| --- | --- | --- | --- |
| `_dotrace` | `040573bc: 42a7` | — | Application exception, not routine AST return |
| `_trap` call | `04056b94: 61ff` | D2 = 6 | Breakpoint delivery, including TRAP #15 |
| `_psig` exit | `04007d6c: 2f02` | D2 = 5 or 0x85 | SIGTRAP-exit fallback, with/without core flag |
| Frame compaction | `04002126: 204f` | — | Retire original saved-state +0x44 |

Probes use **supervisor instruction decode**, not speculative fetch. A4 is
shadowed through the existing register-file write port; delivery matches
A4+0x44. The exact kernel's entry stubs push 64 bytes of registers plus four
bytes of software metadata, explaining this offset. Its compaction path can
move a format-2 frame four bytes before RTE.

`next_exception_trigger.sv` stores the last **64 candidate/retirement events**
in block RAM. At delivery it freezes history and searches newest-first for
**both URP and frame address**. A retirement prevents an old frame being
reused. Missing/evicted context is reported explicitly; another process's
most recent trace is never substituted.

A match preserves the original exception fields plus delivery kernel PC/SP
and D2 low word. D2 holds consumed PCB trace flags in `_dotrace`, exception
type in `_trap`, or exit status in `_psig`. Old last-RTE fields are omitted
to reduce FPGA routing demand.

## Limits

- This is image-specific, not a process-aware debugger. Validate the exact
  kernel with `python3 tb/check_exception_kernel.py odmach`.
- First delivery is retained even if an application debugger handles it
  without process death. Identify the process from the record and RAM.
- More than 64 intervening events can evict an outstanding frame, for
  example during a lengthy context switch. No user PC is invented.
- SIGTRAP-exit fallback deliberately has no original exception fields. It
  covers software-delivered signals and missed hardware events without
  distinguishing those causes.
- Supervisor trace frames are not candidates. A later kernel delivery can
  therefore have no retained user frame.
- An empty record proves neither CPU correctness nor WindowServer health.

## HPS format (version 3)

Region: physical **0x1ff05000..0x1ff0504f**, Avalon u64-word base
`0x03fe0a00`, outside Ethernet rings ending at `0x1ff04800`.
High/low below means numeric bits 63:32 / 31:0 of each HPS little-endian u64.

| Offset | Contents |
| --- | --- |
| +00 | Magic `0x4e58544449414731` (`NXTDIAG1`) |
| +08 | Captured: 0 armed, 1 complete |
| +10 | Original exception address / stacked PC; zero if unmatched |
| +18 | Frame address / original IR[31:16]:SR[15:0]; low half zero if unmatched |
| +20 | URP / USP (original if matched, delivery-time otherwise) |
| +28 | Delivery kernel SP / instruction PC |
| +30..+40 | Reserved, zero |
| +48 | Header below |

Header: version[63:48]=3, class[47:46]=3 (final), reason[45:44],
reserved[43:36]=0, match[35:32], D2-low16[31:16], reserved[15:12]=0,
original format[11:8], vector[7:0]. Format/vector are zero if unmatched.

Reasons: 1 unhandled trace, 2 breakpoint delivery, 3 SIGTRAP-exit fallback.
Match: 1 frame found, 2 frame already returned, 3 no retained frame,
4 exit fallback without a frame key. Reason 3 has frame address zero.

Payload is written before captured becomes 1; only complete records are
interpretable. Records are immutable until reset. The decoder still supports
previous version-2 captures and their saved last-RTE fields.

The selector holds its entire output stable after capture. The publisher
uses `INPUT_HELD=1` to serialize that held output without a redundant wide
snapshot register. Its default mode still snapshots independently for other
callers. The held-input mode is tested through the real CPU/selector/publisher
chain; selector tests check stability of the complete output record.

## Validation and build

```sh
sh tb/run_exception_tests.sh
python3 -B -m unittest discover -s tb -p test_decode_exception.py
python3 tb/check_exception_kernel.py tb/build/mo_stall/odmach
EXCEPTION_DIAG=1 ./tb/run_tests.sh post
NEXT_EXCEPTION_DIAG=1 /opt/intelFPGA_lite/17.0/quartus/bin/quartus_sh --flow compile NeXT
./release.sh fpu_rev40_diag
```

Directed tests execute real RTE, trace, TRAP, kernel branch decisions and
frame compaction through the CPU, with diagnostic off/on and two bus-wait
settings. Selector tests cover interleaved address spaces, retired/reused
frames, ring wrap/eviction, missing context, first-event retention, and reset.
Mailbox tests exercise the real DDR arbiter, stalls, bridge concurrency,
reset/rearm and interrupted publication. Full POST and device/boot tests
include the new selector.

The FPGA has limited headroom. Aggressive routability optimization, the area
technique and seed 2 are applied by the opt-in block in files.qip. The revision-0x40 build uses 41,023/41,910 ALMs (98%), compared
with 40,852 for the preceding MLAB diagnostic build, and 1,280 MLAB memory bits.
Clocks and timing constraints are unchanged. `release.sh` refuses staging
when the timing gate fails. Quartus success alone is not a timing pass.
No deployment, reset, commit or push is automatic.

The revision-0x40 build passed: worst setup +0.413 ns, hold +0.197 ns,
recovery +3.672 ns, removal +0.912 ns, minimum pulse width +0.714 ns.
Build/test reports and source snapshots are in `tb/build/rbf_fpu_rev40_aoBbdR/`.
The preceding MLAB build and its reports remain available as
`releases/NeXT_20260915_unhandled_mlab_0ba8f7c.rbf` and
`tb/build/rbf_mlab_diag_rSXsQN/`.
Import-specific CPU/FPU and real FPSP tests are in
`tb/build/import_fpu_mlab_TGf9uU/`.

The preceding non-MLAB version remains available as
`releases/NeXT_20260915_unhandled_diag_95e29fb.rbf`, with its original
reports and source snapshots in `tb/build/rbf_unhandled_diag_J5Tlxj/`.
