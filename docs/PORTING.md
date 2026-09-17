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
| `src/dma.c`                | `rtl/next/next_dma_stub.sv`| remaining channels: pointer registers are plain storage, CSRs read 0, plus the frame interrupt (video channel limit 0xEA raises INT_VIDEO at VBL, CSR write releases). Ethernet, disk, printer and both codec sound channels have real engines in their device modules |
| `src/scc.c` serial (8530)  | `rtl/next/next_scc.sv`     | register-level model: shared register pointer, WR9 reset commands, RR0 status, one-byte local loopback (the scc.c model the boot ROM test passes against). No baud timing, interrupts, or DMA |
| `src/esp.c`, `src/scsi.c`  | `rtl/next/next_scsi.sv`    | ESP (53C90) with the initiator command set (select with/without ATN, transfer info PIO+DMA, pad, ICCS, message accepted, bus reset, selection timeout), the SCSI disk target (TEST UNIT READY, INQUIRY, REQUEST SENSE, READ CAPACITY, READ/WRITE 6+10, MODE SENSE, START/STOP, FORMAT), the NeXT SCSI DMA channel, and the disk image on the MiSTer SD card (hps_io block access, OSD slot "SCSI Disk") |
| `src/ethernet.c` (MB8795)  | `rtl/next/next_enet_dma.sv`| registers, both DMA channels with chaining, the local loopback path (TXMODE_DIS_LOOP clear) with EN_EOP framing, minimum-size padding and place-holder CRC, the receive address filter, and the frame streaming interface to the bridge |
| real network               | `rtl/next/next_enet_bridge.sv`, `next_ddram_arb.sv` | when the guest disables loopback, frames cross a DDR3 shared-memory mailbox (rings at 0x1FF00000, the A2065 window) to an ARM daemon in Main_MiSTer (`support/next/next_enet.cpp`, branch `next-ethernet` of the Main fork) that bridges to eth0 (BPF filtered), eth1, a macvlan child, or tap0 -- the architecture of the Minimig A2065 support, with the NIC kept in the fabric and only frames crossing. The OSD "Network" option (status bits [54:52]) selects the interface |
| `src/mo.c` optical drive   | `rtl/next/next_mo.sv`      | OSP registers, disk DMA channel, ECC buffer engine in the standalone MOCSR2_ECC_DIS mode (fill from and drain to memory), the shared Reed-Solomon codec (next_rs.sv: reads decode, writes encode the 1296-byte sectors), and disk read/write of the SD image |
| `src/floppy.c`             | `rtl/next/next_floppy.sv`  | Intel 82077AA: command and result phases through the FIFO, specify, configure, recalibrate, seek, sense interrupt status, drive status, read id, read and write with the sector data on the shared SCSI DMA channel, geometry derived from the image size (720K, 1440K, 2880K), format (consumes C/H/R/N descriptors and zero-fills the sectors), image from the MiSTer SD card (OSD slot "Floppy"). Scan is reported invalid, as in Previous (which does not implement it) |
| `src/kms.c`, `src/snd.c`   | `rtl/next/next_kms_snd.sv` | KMS status/control bytes, command/data pairs, keyboard input (PS/2 to NeXT keycodes, modifiers, device poll mask, set-address protocol, overrun), sound out enable/disable, and the sound out DMA channel engine with completion interrupt and underrun status. Mouse input (PS/2 packet to NeXT mouse report), and a 44.1 kHz stereo audio-out path (sound-out DMA frames to a FIFO drained to audio_l/r) |
| `src/snd.c`, `src/dma.c` | `rtl/next/next_snd_in.sv`, `rtl/next/next_audio_adc.sv` | Codec recording: ADC-IN or silence, 8,012 Hz mono mu-law, sound-input DMA channel lifecycle and completion interrupt. See [audio input](AUDIO_INPUT.md) |
| `src/dsp/` DSP56001        | -                          | TODO (reads return 0) |
| `src/nbic.c` NeXTbus       | -                          | TODO (bus error, equivalent to a machine without NBIC) |
| `src/printer.c`            | `rtl/next/next_printer.sv` | partial: LP CSR (0x0200F000) has writable power/interface controls and is decoded separately from LP data (0x0200F004); no printer responses, so the driver probe times out. The DMA-out channel (0x02000090) consumes raster data with COMPLETE/INT_PRINTER_DMA and SUPDATE chain reload; the physical printer protocol is not implemented |

## What provably works

The complete boot ROM power-on system test passes: in full-system
simulation (Verilator driving the real RTL and the real Rev 2.5 v66
ROM), POST runs FPU, SCC, SCSI, ethernet loopback, ECC
(Reed-Solomon correction of 36 injected byte errors), RTC, hardclock
timer, and event counter tests, prints the "System test passed" path,
and drops into the ROM monitor loop.

Two system-level behaviors were required beyond the device models:
- CPU caches enabled with the real 68040 cacheability model
  (cache_allow_all, gated by CACR and the MMU cache-inhibit
  attributes); the ROM's calibrated delay() loop runs from the
  instruction cache.
- CPU pacing (CPU_PACE_* in next_system): internal cycles advance 1 of
  every 2 clocks so the cached DBF timing loop runs at real-25MHz-68040
  speed; the POST event counter test measures delay(1000) against a
  899..1100 us window and gets ~1004 us.

`tb/run_tests.sh` (Verilator) runs the real RTL, no mocks
(`./run_tests.sh post` also runs the full power-on system test to the
passed path, about 5 minutes):

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
- `tb_next_rs`: the Reed-Solomon codec against golden vectors
  generated by the reference implementation itself (Previous src/rs.c
  compiled standalone, tb/gen_rs_vectors.c): encode is bit-identical,
  decode corrects the POST's exact 36-error corruption pattern.
- `tb_next_scc`, `tb_next_esp`, `tb_next_enet`, `tb_next_mo`,
  `tb_next_snd`: each replays the corresponding boot ROM system test
  sequence (from the ROMV66 listing) against the real device module:
  SCC init table and loopback, ESP FIFO/flags and transfer counter,
  ethernet DMA loopback packet, ECC buffer fill/drain round trip, sound
  out DMA completion and underrun.  The v66 POST fails the machine on
  the first broken device, so these are what stand between power-on and
  the boot monitor.

## Design notes

- Two clock domains: `clk_sys` 28 MHz for CPU, devices and DDR3 (the
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
- The CPU runs with its internal caches enabled (cache_allow_all=1:
  the real 68040 model, cacheability from CACR and MMU attributes,
  not physical windows).  DMA writes to RAM are snooped into the data
  cache.  Cache line fills go through the normal 16-bit bus.
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

1. (done) KMS keyboard input: PS/2 events post NeXT keycode events
   through the KMS data path with INT_KEYMOUSE. F10 uses the separate
   INT_POWER bit 2 (IPL 3), including when keyboard polling is disabled;
   Delete is deliberately unassigned. See the keyboard mapping in README.
2. (done) SCSI: ESP (53C90) + DMA channel + disk image from the
   MiSTer SD card (hps_io block access) in next_scsi.sv.
3. (in progress) Boot NeXTSTEP from the SCSI disk: the OSD "Boot
   device" setting is evaluated and written to battery-backed NVRAM on a
   user reset. Auto selects a mounted SCSI hard disk on target 0, 1, or 2,
   then a valid floppy, and otherwise writes an empty command for the ROM's
   default order. Explicit choices write Disk (`sd`), Floppy (`fd`), Network
   (`en`), ROM Default (empty), Optical (`od`), or a qualified CD-ROM probe
   command. The v66 ROM does not complete a direct CD boot: installation
   boots the installer floppy with the CD as root media. A guest CPU `RESET`
   resets devices without rewriting NVRAM; use a user reset to apply the OSD
   choice. The ROM's SCSI boot path (select, sector reads by DMA) runs in
   simulation with "./run_tests.sh bootsd"; a real NeXTSTEP image is needed
   for an actual boot.
4. (done) KMS mouse input: PS/2 packets become NeXT mouse reports.
5. (done) Sound output path, mouse input, and the printer DMA channel
   (next_printer.sv). The printer command/response protocol, SCC serial
   data path, and DSP56001 remain stretch goals.
6. CPU caches on (cacheable windows for RAM/ROM/VRAM), snoop from DMA
   writes.
