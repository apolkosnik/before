# SCSI, FPU and ALU resource sharing

This pass preserves CPU/FPU state sequencing, arithmetic precision, clocks,
MMU/cache behavior and guest binaries. It does not address the outstanding
hardware hang at `Checking disks`. Changes are uncommitted; see the later
cold-boot hardware result below.

The measurements below are for this three-change snapshot only. The later
[AP040 import](AP040_IMPORT.md) adds integer-register MLAB storage and shared
exception entry; those additions are not in this report's RBF or area totals.

## Changes

1. SCSI geometry: replace the mount-time serial divider and pending-slot
   scheduler with `ceil(blocks / 128)`, expressed as a slice plus a rounded
   increment. Retain the existing 32-bit block-count truncation and 24-bit
   cylinder wrap. Metadata updates on the mount edge, including simultaneous
   slots and consecutive replacements; guest transfer sequencing is unchanged.
   Quartus already merged the old `mnt_size` and `img_blocks_v` arrays, so
   removing the former is not counted as a second array's worth of savings.
2. FPU normalization: source, restored destination, and add/subtract result
   select their input before one shared leading-zero encoder and 67-bit left
   shifter. Existing states, GRS handling and exponent updates are retained.
3. ALU: ADD/ADDX share an addition, and SUB/SUBX/CMP share a subtraction,
   selecting the incoming X bit before arithmetic. Sticky Z and CMP's preserved
   X/destination remain unchanged. BCD, NEG/NEGX and shift/rotate paths are
   untouched.

## Measurement method

Evidence directory: `tb/build/alm_sharing_1b4lk0/` (ignored build artifacts).
The baseline snapshot was taken before editing these three RTL files, from
parent commit `5d94495214417d36ff16cd06a87f2be4a9006d38` and AP68040 commit
`880b81cc02f728c477fb772c218e10140106dcd9`. The source archive, original hashes,
per-stage projects and compiler logs are retained there.

Both full fits use Quartus 17.0.2, device 5CSEBA6U23I7, the same QSF/SDC and
build-date header, diagnostics enabled, AREA synthesis, aggressive routability
ALWAYS, seed 3. These reproduce the previous diagnostic build's settings;
the main project's configuration and clocks were not edited. This is a fresh
paired comparison, not a subtraction from a differently configured old RBF.

Synthesis reports use **ALUTs**, not fitted ALMs. Their hierarchy counts can
also move in otherwise unchanged modules because of whole-design optimization.
Only the completed paired fits establish the overall usable ALM reduction.

| Stage | SCSI ALUTs | FPU ALUTs | ALU ALUTs |
| --- | ---: | ---: | ---: |
| Baseline | 4,085 | 7,640 | 2,485 |
| SCSI only | 3,930 | 7,633 | 2,464 |
| SCSI + FPU | 3,913 | 7,220 | 2,456 |
| All three | 3,866 | 7,154 | 2,349 |

Both full builds completed successfully. The paired fitted result is:

| Resource | Baseline | All three changes | Reduction |
| --- | ---: | ---: | ---: |
| ALMs | 39,130 | 38,560 | 570 |
| Occupied LABs | 4,172 | 4,149 | 23 |
| M10Ks | 487 | 486 | 1 |
| Registers | 26,437 | 26,387 | 50 |
| DSP blocks | 65 | 65 | 0 |

The device has 41,910 ALMs and 4,191 LABs. Nominal free ALMs rise from 2,780
to 3,350, and completely unused LABs from 19 to 42. This is still a densely
packed design: free ALMs are not a guarantee that a future feature will route.

Fitter hierarchy attribution (inclusive ALMs needed): SCSI 2,498.8 → 2,397.6;
FPU 5,065.7 → 4,732.4; ALU 1,638.7 → 1,530.6. These are fractional packing
attributions, not independently fitted blocks. The removed M10K is the old
six-entry `geo_cyl_v` RAM; the new mount update no longer infers it.

Worst reported setup slack is +0.298 ns → +0.111 ns (HDMI), and hold slack
+0.248 ns → +0.247 ns. All reported timing categories pass, with no clock
reductions or new timing exceptions. Both builds retain the existing timing
constraints; passing constrained paths is not a claim that every external
interface is constrained (TimeQuest reports unconstrained paths).

The experimental comparison RBF is
`tb/build/alm_sharing_1b4lk0/combined/output_files/NeXT.rbf`, SHA256
`5aa9e17e30c397d65793781cf63f31ee89abef3ecdb02111df4ac138265b10eb`.
It uses the diagnostic configuration above and remains outside `releases/`.
The fit is one matched seed pair,
not a multi-seed guarantee. Baseline/optimized build times were 17:32 / 27:15;
this is compile time, not guest execution time.

Subsequent hardware result (September 16): the user reports this exact
combined RBF boots after a full MiSTer power cycle. Earlier core-reload-only
tests had stalled at `Testing system...`, including an older 95e29fb RBF.
Read-only SSH confirmed a new Linux boot ID and a now-valid DDR diagnostic
mailbox marker after the successful cold boot. This supports a persistent
reload/reset-state issue, but its trigger is not yet isolated or fixed.
No later AP040 imports are included in this hardware result.

## Validation

- SCSI geometry: 4,110 transactions, checking all six slots, including
  rounding, overflow, ignored size bits, simultaneous mounts, rapid remounts,
  ejection and reset. `tb/tb_next_scsi_geometry.sv` is in the main NeXT runner.
- Full NeXT device/short-ROM-boot/recording suite with diagnostics: pass.
- Full AP68040 suite, including MMU/MOVEM/bitfield/cache tests and both FPU
  frame revisions under all three bus phases: pass.
- FPU normalizer: 10,336 cases plus clock-enable holds pass against a
  serial-shift oracle, on both baseline and optimized RTL.
- ALU: 1,479,840 arithmetic-oracle cases plus 1,048,576 all-opcode
  differential cases: 2,528,416 comparisons pass. The optional differential
  leg uses `-DALU_REFERENCE` and a mechanically renamed baseline module
  (`ap040_alu_reference`). No copied implementation is kept in tracked tests.
- Existing WinUAE-backed BUSY-resume oracle: all 288 vectors pass in all
  three bus phases. This reuses the earlier fixed oracle, not a new WinUAE run.
- Real-kernel FPSP matrix: all 24 positive configurations and four paired
  revision controls pass. Known guest packed-decimal failures remain excluded;
  no claim is made that those are fixed by resource sharing.
- Exact Improv libc/kernel `sscanf("0", "%f")` regression: pass with both
  memory latencies and both stack fills (four configurations).
- Cycle checks match exactly before/after: integer benchmark
  260,856 / 261,634 / 261,634; FPU suite 150,372 / 202,646 / 202,646.

Run the tracked regressions with:

```sh
VASM=/opt/amiga/bin/vasmm68k_mot sh rtl/AP68040/tb/run_tests.sh
EXCEPTION_DIAG=1 sh tb/run_tests.sh
sh tb/run_fpu_revision_tests.sh /absolute/sdmach /absolute/odmach
```

Kernel fixtures are external and hash checked. Full logs and the paired
reports live in the evidence directory; no release RBF is replaced.
