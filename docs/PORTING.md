# Porting Previous to MiSTer: status and roadmap

This core implements the NeXT hardware in FPGA logic, using the
[Previous](https://github.com/probonopd/previous) emulator (vendored as
the `reference/previous` submodule) as the behavioral reference, the
[AP68040](https://github.com/apolkosnik/AP68040) CPU core (submodule at
`rtl/AP68040`) as the processor, and the
[MiSTer template](https://github.com/MiSTer-devel/Template_MiSTer) as
the platform scaffold.

Target machine: NeXTcube 68040 25MHz, monochrome, non-turbo
(`NEXT_CUBE040` in Previous terms, SCR1 = 0x00012052), boot ROM
Rev 2.5 v66 (`reference/previous/src/Rev_2.5_v66.BIN`).

## Module map: Previous source to RTL

| Previous source            | RTL                        | Status |
|----------------------------|----------------------------|--------|
| `src/cpu/*` (WinUAE 68040) | `rtl/AP68040` (submodule)  | done (real CPU core with MMU, FPU, caches) |
| `src/cpu/memory.c` map     | `rtl/next/next_system.sv`  | done: ROM, ROM mirror, IO + BMAP IO mirror, BMAP + 0x820C alias, RAM banks, RAM/VRAM MWF mirrors, bus error elsewhere |
| `src/sysReg.c` SCR1/SCR2   | `rtl/next/next_scr.sv`     | done |
| `src/sysReg.c` interrupts  | `rtl/next/next_intc.sv`    | done, including the TIMERIPL7 promotion |
| `src/sysReg.c` hardclock   | `rtl/next/next_timer.sv`   | done |
| `src/sysReg.c` event ctr   | in `next_system.sv`        | done (microsecond counter with byte-0 read latch) |
| `src/rtcnvram.c`           | in `next_scr.sv`           | done: MC68HC68T1 serial protocol, 32-byte NVRAM with the default image (valid checksum), BCD time-of-day counter. Date registers are static defaults; no alarm, no power-down |
| `src/bmap.c`               | `rtl/next/next_bmap.sv`    | done (register file plus heartbeat bit) |
| `src/video.c` + real HW    | `rtl/next/next_video.sv`   | done: 1120x832 2bpp scan-out at 68.5 Hz from VRAM, VBL |
| VRAM                       | `rtl/next/next_vram.sv`    | done, 256 KB BRAM, CPU port + scan port |
| boot ROM                   | `rtl/next/next_rom.sv`     | done, 128 KB BRAM, loaded via OSD (boot file) |
| main RAM                   | `rtl/next/next_ddram.sv`   | done, 64 MB in HPS DDR3 (4 banks x 16 MB, fixed full size) |
| `src/dma.c`                | `rtl/next/next_dma_stub.sv`| stub: pointer registers are plain storage, CSRs read 0. Real behavior implemented only for the frame interrupt (video channel limit 0xEA raises INT_VIDEO at VBL, CSR write releases) |
| `src/kms.c` keyboard/mouse | -                          | TODO (reads return 0) |
| `src/esp.c` SCSI (53C90)   | -                          | TODO (reads return 0) |
| `src/scc.c` serial (8530)  | -                          | TODO (reads return 0) |
| `src/ethernet.c` (MB8795)  | -                          | TODO (reads return 0) |
| `src/mo.c` optical drive   | -                          | TODO (reads return 0) |
| `src/floppy.c` (82077AA)   | -                          | TODO (reads return 0) |
| `src/dsp/` DSP56001        | -                          | TODO (reads return 0) |
| `src/nbic.c` NeXTbus       | -                          | TODO (bus error, equivalent to a machine without NBIC) |
| `src/printer.c`, `src/snd.c` | -                        | TODO |

## What provably works

`tb/run_tests.sh` (iverilog) runs the real RTL, no mocks:

- `tb_next_boot`: the real Rev 2.5 v66 boot ROM executes from reset on
  the real AP68040 through the real decode/devices.  Verified: reset
  vectors fetched from ROM 0/4, entry at 0x0100001E, BMAP setup
  (including the 0x820C0000 alias the ROM uses), SCR1 machine id read
  back as 0x00012052, SCR2 writes, no double faults.  Main RAM is a
  64 MB model behind the same ram_* port the DDR3 adapter serves on
  hardware.
- `tb_next_video`: line/frame timing (1600x912 at 100 MHz = 62.5 kHz /
  68.5 Hz), 1120x832 active, 2bpp gray decode, 288-byte line pitch, VBL
  pulse per frame.
- `tb_next_hardclock`: microsecond period accuracy, INT_TIMER at IPL 6,
  CSR-read release, periodic refire, TIMERIPL7 promotion to IPL 7.
- `tb_next_rtc`: the boot ROM's bit-banged serial protocol against the
  NVRAM default image (auto-increment, checksum bytes, write/readback).

## Design notes

- Two clock domains: `clk_sys` 32 MHz for CPU, devices and DDR3 (the
  AP68040 closes timing around 30 MHz on this device; the proven
  Minimig-AGA integration runs it at 28.7 MHz), and `clk_vid` 100 MHz
  for the pixel pipeline (CE_PIXEL = 1).  1600x912 total at 100 MHz
  gives 68.5 Hz against the real monitor's 68.3 Hz.  The only domain
  crossings are the dual-clock VRAM scan port and the synchronized
  vertical blank level.
- CPU bus: the TG68K-shaped 16-bit port of `ap040_tg68k_compat`,
  protocol exactly as in `rtl/AP68040/tb/tb_ap040_program.v`
  (mem_ready pulse, clkena = idle | ready | berr, level berr held until
  the bus goes idle).  The MMU table walker port is served by the same
  RAM path (the core never runs both at once).
- The CPU currently runs uncached (no cacheable windows declared, so
  no line fills).  Enabling the caches needs the cache_* burst port
  wired to RAM and cacheable window configuration - a straightforward
  next step once POST is fully green.
- Unmapped addresses bus-error, as in Previous (the ROM uses bus
  errors to probe for hardware).
- The AP68040 `primitives/dpram.v` is a plain behavioral model that
  does not map to Cyclone V M10K true dual port (it falls into tens of
  thousands of registers).  As its README suggests, the host project
  substitutes its own: `rtl/next/dpram.v` is the standard Intel true
  dual-port inference template (per-port write-first).  The testbenches
  compile the same file, so simulation runs with the semantics the FPGA
  gets.  Big storage (`next_vram`, the DMA stub register file) is built
  from byte-wide arrays coded on the same template, because byte-lane
  writes into one 16-bit array and 1-write-2-read patterns either do
  not infer or get duplicated by Quartus 17.
- MWF mirrors (memory write functions 1-3, raster ops) are plain
  writes for now; function 0 (copy) is correct by construction.

## Roadmap to a booting system

1. KMS (keyboard/mouse/sound box): register interface from
   `src/kms.c`, keyboard from `ps2_key`, needed for the ROM monitor.
2. DMA engine (`src/dma.c`): real channel state machines, starting
   with SCSI.
3. SCSI: ESP (53C90) + DMA + disk image from the MiSTer SD card
   (hps_io block access), to boot NeXTSTEP.
4. SCC, sound, ethernet, DSP as stretch goals.
5. CPU caches on (cacheable windows for RAM/ROM/VRAM), snoop from DMA
   writes.
