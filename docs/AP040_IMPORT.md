# AP040 register-file and exception-entry import

Imported into the uncommitted AP68040 working tree from the local
`/home/adam/ap040x2` branch `ap040-40mhz`, in this order:

| Commit | Change |
| --- | --- |
| `3f235e5d0e86a569f093109629a15ff15b5aa4ec` | Integer registers in mirrored MLAB banks; separate stack pointers and debug shadows |
| `eb158fb717feb136326b65ac6a8a6ae54ab3e90a` | One shared exception-entry block using per-cycle request carriers |
| `0c63ec3c2502a06933ed0f50304b30915e6ad1c4` | Delay the MLAB write and bypass pending data; remove integer-bank `no_rw_check` |

The repeated `eb158fb…` in the initial request was applied once. No other
branch commits were imported. The final `ap040_core.v` and `ap040_regfile.v`
are byte-identical to those files at `0c63ec3…` (Git blob IDs
`59ab1f55a3fa18831a7a888c9fff9e3fdaa6663f` and
`4ddf84bc047aca19a14406489a247266853cbf7e`, respectively).

Existing uncommitted SCSI, FPU-normalizer and ALU-sharing optimizations are
preserved. No commit, branch change, push or hardware deployment was made.
The source repository is untouched. The register-file commit's Amiga-specific
bench edits do not apply to this tree; the corresponding NeXT FPSP diagnostic
reads were adapted to the public D0/D1 debug taps and pending-aware A6 view.

## Validation

Evidence: `tb/build/ap040_import_sJqPuQ/`, including the pre-import core,
register file, ALU/FPU files, program simulator and CPU logs.

- Full AP68040 regression suite: pass, including both FPU frame revisions.
- All ten CPU program regressions retain their pre-import cycle counts in
  all three bus phases.
- Full NeXT device, short-ROM-boot and recording suite with diagnostics: pass.
- Real-kernel FPSP matrix: 24 positive configurations and four paired revision
  controls pass. Known guest packed-decimal failures remain excluded.
- Existing WinUAE-backed BUSY-resume oracle: 288 vectors pass in each of three
  bus phases. Exact Improv libc `sscanf("0", "%f")` passes all four latency/
  stack-fill configurations. No guest binary was changed.
- Added standalone register-file regression with an independent architectural
  flip-flop model: normal reads and pending-word poisoning pass; bypass-disabled
  negative control fails as required. Covers both read ports, all registers,
  debug taps, consecutive writes, CE stalls, reset and USP/ISP/MSP selection.
  Each positive leg executes 16,485 cycles and 1,055,008 port checks; the
  poisoning leg hides 7,697 pending RAM words behind the bypass.

The upstream follow-up describes a hardware boot failure with the initial
MLAB conversion despite passing simulation. The import includes its bypass
fix, but simulation here is not proof of hardware read-during-write timing.
The poison test checks RTL isolation, not an Altera primitive timing model.

## RBF build — 2026-09-17

Built the current uncommitted tree, including these imports and the earlier
[area-sharing changes](ALM_SHARING.md), in the isolated directory
`tb/build/rbf_ap040_import_0AB4cI/`. Source archive, hashes, diffs, reports and
logs are retained there. The earlier cold-booting RBF was not overwritten.

Staged artifact: `releases/NeXT_20260917_ap040_import_0c63ec3_diag.rbf`
(4,409,856 bytes). SHA-256:
`41838bf575a65dfeac85b9707bd6a4a67c85754ddec84513e1a381c051e912a5`.

Quartus 17.0.2, exception diagnostics enabled, AREA synthesis, aggressive
routability ALWAYS, seed 3, matching the earlier area-sharing fitter
settings. The generated build date differs. The full compile passed in
15:13, with zero errors. Fitted usage: **38,004 ALMs, 4,135 LABs, 26,953
registers, 486 M10Ks and 65 DSPs**. This is 556 fewer ALMs and 14 fewer LABs
than the earlier 38,560-ALM area-sharing fit, but 566 more registers.

The timing gate passed: worst reported setup **+0.309 ns**, hold
**+0.244 ns**, CPU-clock setup **+3.771 ns**. Recovery/removal/pulse-width
checks also pass. Existing unconstrained setup/hold paths remain.

Fresh tests of the exact source snapshot passed the full CPU suite and
NeXT device/short-boot/recording regressions. ROM POST reached its success
path at **6387114995000 ps**, exactly matching the prior area-sharing
snapshot. The simulation was deliberately stopped after POST; it did not
run to its 1.4-billion-cycle budget. This build has not been hardware-tested,
deployed, committed or pushed. Use a full power cycle for the initial
hardware test; the earlier reload-state issue is still unresolved.

### Integer MLAB inference caveat

Quartus Info 276009 rejects MLAB inference for `bank_a` and `bank_b` because
of unsupported read-during-write behavior. This RBF implements the integer
regfile with registers: 1,204 dedicated registers versus 576 before the
imports. Thus the intended integer-MLAB saving is **not** realized here.

A separate, isolated two-variant Quartus probe in
`tb/build/mlab_probe_TI044M/` confirms that changing both bank attributes to
`ramstyle = "MLAB, no_rw_check"` infers two 16x32 MLAB memories. Standalone
fitted usage falls from 826 to 393 ALMs and from 1,300 to 276 registers.
Those isolated counts are not a predicted whole-core saving.

The probe annotation changes were **not** in that RBF or its working-tree
snapshot. They were subsequently applied in the follow-up build below.
`no_rw_check` permits unspecified RAM collision output; it does not fix the
collision. The pending-write bypass must hide that output on both ports.
Existing RTL poison tests are not primitive-level timing validation, so
mapped-memory behavior, whole-core timing and hardware boots still require
validation before shipping that change.

## Integer MLAB follow-up build — 2026-09-17

At the user's request, added `no_rw_check` to both integer-bank attributes
and corrected the read-during-write comments. Pending-write and bypass logic,
instruction sequencing, clocks and guest binaries were not changed.

Isolated build: `tb/build/rbf_integer_mlab_1tvOaK/`. Same Quartus 17.0.2,
diagnostics/AREA/aggressive-routability/seed-3 settings and generated date
as the preceding build. The full compile passed in 18:13 after an automatic
routing retry. Both integer banks are now inferred as MLAB memories.

| Resource | Previous RBF | MLAB RBF | Change |
| --- | ---: | ---: | ---: |
| ALMs | 38,004 | 37,729 | -275 |
| Occupied LABs | 4,135 | 4,101 | -34 |
| Registers | 26,953 | 25,971 | -982 |
| MLAB bits | 1,280 | 2,304 | +1,024 |
| M10Ks | 486 | 486 | 0 |
| DSPs | 65 | 65 | 0 |

The integer regfile itself has 180 registers versus 1,204 previously and
uses 40 memory ALMs. The actual whole-core saving is 275 ALMs, not the
433-ALM saving measured in the isolated probe.

Timing gate passed: worst setup **+0.068 ns** (HDMI; narrow positive margin),
hold **+0.228 ns**, CPU-clock setup **+5.059 ns**. Other reported checks
also pass; existing unconstrained setup/hold paths remain.

Fresh CPU and NeXT integration regressions passed. ROM POST reached
**6387114995000 ps**, unchanged; the simulation was deliberately stopped
after success rather than running out the 1.4-billion-cycle budget.
The mapped register-file functional test also passed **16,485 cycles and
1,055,008 port checks** against Intel's Cyclone V MLAB primitives, using an
independent architectural reference. It covers both ports, consecutive
writes, CE stalls, reset, debug taps and stack-pointer banking. This uses
the isolated fitted register-file netlist with identical functional RTL,
not the full NeXT netlist. Hierarchical RTL poisoning is not used in this
mapped test, and read checks allow 25 ns settling time.

Quartus rejected timing-netlist export with Warning 10905 (functional
netlists only for this device), so the mapped test is **not SDF timing
simulation** and does not establish full-core operating frequency.
Physical timing is covered by STA; actual hardware boots remain untested.

Artifact: `releases/NeXT_20260917_integer_mlab_diag.rbf` (4,410,148 bytes).
SHA-256: `2d83723dfe981d4dc9ad1ae2ba3739f96a85b6fd4163354f4323d0b486230ed2`.
The previous RBF is preserved. No commits, pushes or deployment were made.
Use a full power cycle for the initial hardware test; the reload-state
issue remains unresolved.
