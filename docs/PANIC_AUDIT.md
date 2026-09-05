# SCSI-write panic audit

The saved NeXTSTEP 3.3 panic stack identifies a packed-decimal FPU exception
inside `libsys_s.B.shlib::__dbltopdfp`. AP68040 raised this exception without
preparing the FPU state frame required by the kernel's software handler.
A focused test running the actual NeXT handlers reproduces missing stores
before the fix and completes correctly afterward. The FPGA rebuild is
complete. Full-disk A/B runs remain in progress; hardware recovery is not
yet confirmed.

The earlier conclusion that an overwritten kernel function pointer caused
the supervisor-fetch fault at `0x3c014d9c` is not supported by the dump.

## Packed-decimal exception finding

The saved FPSP frame pointer `0x1013bd14` corresponds to physical
`0x04663d14` in the captured thread's kernel stack. The bytes above it are
an exception frame, **not a C call frame**:

```text
04663d14  03fffc6c                     saved user A6
04663d18  0018 0505429a 30dc 03fffc60   SR, next PC, format/vector, operand EA
```

This explains the misleading traceback `called from pc 00180505`:
that value concatenates SR and the high half of PC. The original user-mode
exception was vector 55 (unsupported FPU datatype), format 3, immediately
after the instruction at `0x05054296`.

The reproducer's library independently supplies the exact instruction:

```text
05054288  __dbltopdfp
05054296  f210 7c00    fmove.p fp0,(a0){d0}
0505429a  4e75         rts
```

The handler's saved FP0 is `3ffc0000 8103c800 00000000`, and saved D0 is
`ffffffff` (dynamic k-factor -1). These values are included in the focused
regression. The corrected handler stores `40010001 00000000 00000000`
(0.1, rounded to one decimal place) and returns with balanced stacks.

Three related defects are corrected:

1. Packed stores bypassed the FPU using `fp_force_unsupp`. The CPU built the
   format-3 exception frame, but the FPU did not build its separate 100-byte
   BUSY state frame. The kernel's `FSAVE` therefore emitted IDLE, while FPSP
   continued accessing the command and operands at BUSY-frame offsets in
   stale stack storage. Stores now engage the existing datatype capture path
   before delivering vector 55, preserving postincrement/predecrement and EA.
2. Packed sources used a zero placeholder instead of their twelve fetched
   bytes. Match `reference/previous/src/cpu/fpp.c::fp_unimp_datatype`: first
   longword in FPTEMP_LO, remaining words in ETEMP_HI/LO, with the packed tag
   and E1 bit. The corresponding short unimplemented-instruction frame also
   preserves that layout for FPSP-emulated operations on packed sources.
3. A store can update FPIAR and capture its datatype frame on the same edge.
   FPIARCU now forwards that update instead of saving the previous FPIAR.

`tb/next_fpsp.s` runs the unmodified kernel FPSP, including its real
`copyin`/`copyout` glue in user mode. No disk/DMA or MMU translation is involved.
It checks FSIN, FINT, FINTRZ, packed stores/loads, packed FINT, and the captured
dynamic-k operation in supervisor/user modes with two memory latencies and
zero/pattern-filled stacks (eight configurations). The kernel is
an external fixture, hash-checked and loaded by Mach-O segments with zeroed
BSS; no kernel binary is added to the repository.

```sh
sh tb/run_next_fpsp.sh /absolute/path/to/sdmach
python3 tb/panic_audit.py /path/to/sdmach --ram /path/to/next_ram.bin \
  --libsys /path/to/libsys_s.B.shlib
```

The standalone CPU suite also checks the generated BUSY-frame header,
command, E1/T flags, operands, packed-source layout, and FPIARCU, without
requiring NeXT binaries. The missing-frame regression failed before the
store fix; all CPU and device tests pass after the fixes.

### Separate known FRESTORE replay gap

The optional `CHECK_REPLAY=1 sh tb/run_next_fpsp.sh /absolute/path/to/sdmach`
adds `FADD.P 0.1,FP1` with FP1 initially 1.0. NeXT normalizes the operand and
hands the pending arithmetic operation back through FRESTORE. AP68040 does
not execute that restored operation: FP1 remains 1.0 instead of 1.1.
This diagnostic intentionally fails at stage 6. It is a pre-existing broader
FPU integration gap, not claimed fixed by the packed-store panic changes;
the library conversion from the captured panic does not require this replay.

### Packed-decimal build and validation

- Full AP68040 suite: **ALL TESTS PASSED**, including new packed-frame
  checks 650–665. Log: `tb/build/fpsp_cpu_tests.log`.
- NeXT devices and both boot smoke configurations: **all tests passed**.
  Log: `tb/build/fpsp_device_tests.log`.
- Actual-kernel FPSP harness: **8/8 configurations pass**.
  Log: `tb/build/fpsp_kernel_tests.log`.
- Baseline CPU/FPU source fails packed-store stage 2, showing
  `busy=0 prepared=0` at vector 55 and leaving the destination untouched.
  Log: `tb/build/fpsp_baseline.OmgSgw/result.log`. Baseline files were
  compiled separately; the working RTL was not reverted for this check.
- Opt-in FRESTORE replay reproducer still fails stage 6 as documented.
  Log: `tb/build/fpsp_replay_known_failure.log`.
- Quartus full compilation completed **2026-09-04 23:01 EDT**, 0 errors,
  170 warnings. Constrained worst setup/hold slack: **+0.347/+0.223 ns**;
  recovery/removal: +4.228/+1.056 ns; minimum pulse width: +0.714 ns.
  TimeQuest still notes incomplete setup/hold constraints; these numbers
  are not a claim that every I/O path is constrained.
- Resource use: 38,382/41,910 ALMs (92%). Build log:
  `tb/build/fpsp_quartus.log`.
- Artifact: `output_files/NeXT_20260904_fpsp.rbf`, also available as
  `output_files/NeXT.rbf`; 4,392,868 bytes. SHA-256:
  `c9a891aab9051044324c1cf2ecf4a29acd02a02775cb30b059745ccb10129b19`.
- Old full-disk run: `tb/build/panic_fsck.CTMdc1/boot.log`.
  Fixed full-disk run: `tb/build/panic_fixed.puurg6/boot.log`.
  Both use private copies of the original reproducer and continue beyond
  kernel entry. No successful fsck completion is claimed yet.

## Corrections to `tb/build/panic_handoff.md`

The extracted `/sdmach` is a symbol-bearing big-endian m68k Mach-O executable.
Its symbols and disassembly establish the following:

| Earlier inference | Evidence from the actual kernel |
| --- | --- |
| `0x04014740..0x0401aa4f` is destroyed resident text | `_init_TEXT_BEGIN = 0x04014728`, `_init_TEXT_END = 0x0401aa64`. `_main` at `0x04018950` passes these exact endpoints to the routine at `0x0406be48`. Its alignment computes **exactly** `0x04014740..0x0401aa4f` as reusable storage. Kernel structures and zeros here do not establish corruption. |
| `0x040b62b0` was a function pointer to `0x04014d9c` | This address is in `__bss`. `_dbg_trap` clears it at `0x040a1964`, then copies exception-frame address fields into it. `_client_pcb` also contains the reported fault PC at `0x040ca7b4`. Both record the panic. |
| The bad pointer gained `0x38000000` | No original pointer value was captured. `0x04014d9c` is an instruction inside initialization code (`addq.l #1,d1`), and does not occur as a literal pointer in the clean kernel. Matching low address bits does not establish a before/after relationship. |
| Simulation identified the corrupting CPU copy | The old bench treated **any** PC in `0x04xxxxxx` as kernel entry. That includes the disk bootloader. It stopped during correct copying at `0x04381930`, before the kernel entry from `LC_UNIXTHREAD`, `0x040002cc`. |
| A PC outside physical RAM/ROM proves an invalid fetch | The CPU debug PC is virtual. Only translation and access attributes determine whether a virtual fetch is valid. |

The reproducer's `/sdmach` was independently read through UFS and compared
against the saved reference: all 836,568 bytes match (SHA-256
`f1c68dcb7e99e71c7ada5b1ca733b238b90ed337e8fb9512161e2a7120090ddb`).

Reproduce the section/symbol checks without modifying either input:

```sh
python3 tb/panic_audit.py /path/to/sdmach --ram /path/to/next_ram.bin
```

The cleanup call in `_main` disassembles as:

```text
04018950  pea.l  $0401aa64
04018956  pea.l  $04014728
0401895c  bsr.l  $0406be48
```

The reproducer's `/usr/standalone/boot` independently confirms the copy PC:
its `__TEXT` maps file offset `0x294` to `0x04380000`. At `0x0438192e` it
executes eight `move.l (a0)+,(a1)+` instructions, followed by `subi.l #32,d0`
and a loop branch at `0x04381944`. This is the bootloader copy captured by
the original watch, not a runtime kernel relocation routine.

The cleanup routine is `_zone_freepinned_space`. It rounds the start using
`(start + 31) & ~15`, and the end using `(end - 15) & ~15`. Its resulting
interval matches the supposedly
corrupt span byte for byte. The handoff's intact disk comparison is useful,
but neither it nor this dump rules out a CPU, MMU, cache, or DMA defect.

Comparing the saved RAM against `/sdmach` over `0x040012d4..0x040ae56b`,
excluding the reclaimed interval, finds **zero differing bytes**. This
range skips the initial kernel stack and includes the remaining text,
strings, and constants. There is no evidence here of permanent kernel
code being overwritten.

## Confirmed cache defect and bounded fix

Repeated snoops can occupy tag RAM port B until after a CPU write-through
store has completed in memory. The first-row store invalidation remains
pending in `store_inv_lost`. Previously, a following read could nevertheless
enter `C_LOOK`, using a still-valid old tag and pre-store cache data. Even if
the pending invalidation lands on the read-acceptance edge, the tag RAM's
old-data output makes that read stale.

Both read acceptance and the corresponding FSM transition now wait for
`store_inv_lost` to clear. Stores retain their existing handling; snoops keep
their priority and clock-enable-independent operation.

Directed test T10 issues consecutive snoops across a store acknowledgement,
then reads the same address with no request-low gap. It sweeps four memory
latencies and four snoop end timings. All 16 cases returned the previous value
before the fix; all pass afterward with both the standalone and NeXT RAM
models. Existing T1–T9 also pass.

```sh
sh tb/run_cache_tests.sh
VASM=/opt/amiga/bin/vasmm68k_mot sh rtl/AP68040/tb/run_tests.sh
sh tb/run_tests.sh
```

This is a demonstrated cache correctness bug, **not a demonstrated cause of
the NeXT panic**. The directed test uses continuous snoops; NeXT serializes
CPU and DMA RAM transactions and cannot generate every generic cache-port
interleaving. A trace of the failing control transfer is still needed.

## Improved reproduction diagnostics

`tb_next_boot` now recognizes the actual NS3.3 kernel entry. Use
`+kernelentry=<hex>` for another kernel. `+stopkernel` stops there, after loading.

The physical write watch requires an explicit `+guardlo=<hex>` and
`+guardhi=<hex>`. Do not use initialization text as a permanent-text guard.
`+guardhalt` stops on a selected write; it does not establish that it is wrong.

`+panictrace` records the last 64 CPU memory completions, including cache hits:
instruction PC/opcode, CPU state, read/write, size, logical/physical address,
and data consumed. `+wildhalt` stops when the supervisor PC reaches the
reported panic target, default `0x3c014d9c`, configurable with `+wildpc=<hex>`.
The matching supervisor instruction-walk failure also dumps its descriptors
and the CPU trace, and is now counted as a test failure.

Run against a **copy** of the reproducer: the bench writes sectors back to its
`+img` file. For example, from `tb/` after building the bench:

```sh
build/vl_tb_next_boot/tb_next_boot +bootsd +img=/path/to/test-copy.vhd \
  +panictrace +wildhalt +mcycles=12000
```

The next useful evidence is the control transfer immediately before the bad
PC: whether it came from RTS/RTE, a loaded indirect target, or another CPU
operation, and which memory value supplied that target. A successful FPGA
build or unit test alone cannot confirm the hardware panic is fixed.

## Earlier cache-only validation (superseded build)

- AP68040 suite: all tests passed, including integer, exception, MMU, cache,
  FPU, bus adapter, timeout, and walker tests.
- NeXT device suite and both boot smoke configurations: all tests passed.
- Cache regression: 16 failures before the fix, zero afterward, with both
  standalone and NeXT RAM models.
- Panic trace self-check: a deliberately selected ROM target emitted the
  preceding 15 CPU completions and correctly failed the run.
- A private copy of `NS33_2GB_corrupted.vhd` reached actual kernel entry
  `0x040002cc` at 1,329,524,602 simulated cycles: **ALL PASS**, 1,634 sector
  reads and 209,496 SCSI DMA word writes. The old guard stopped around
  1,135,198,606 cycles, still in the bootloader. This new run deliberately
  used `+stopkernel`; it does **not** validate fsck or filesystem writes.
  Log: `tb/build/panic_run.kNHCuy/boot.log`.
- Quartus full compile completed on 2026-09-04 at 21:37 EDT: 0 errors,
  169 warnings. Worst setup/hold slack: +0.142/+0.134 ns; recovery/removal:
  +3.973/+0.743 ns; minimum pulse width: +0.714 ns.
- Artifact: `output_files/NeXT.rbf`, 4,428,484 bytes. SHA-256:
  `78fc3244c34482545a079c967d82b784068229096652371c307cb3cffadb7eae`.
