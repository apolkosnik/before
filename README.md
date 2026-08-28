# before

NeXT core for MiSTer: a NeXTcube 68040 in FPGA.

Based on the [Previous](https://github.com/probonopd/previous) emulator
as the hardware reference (submodule at `reference/previous`), the
[AP68040](https://github.com/apolkosnik/AP68040) MC68040-compatible CPU
core (submodule at `rtl/AP68040`), and the
[MiSTer core template](https://github.com/MiSTer-devel/Template_MiSTer).

Status: early bring-up.  The real Rev 2.5 v66 boot ROM executes on the
real CPU core through the NeXT memory map, system registers, RTC/NVRAM,
interrupt controller, hardclock, and the 1120x832 monochrome video
pipeline.  See [docs/PORTING.md](docs/PORTING.md) for the module map
and roadmap.

## Building

```
git clone --recurse-submodules <this repo>
cd before
quartus_sh --flow compile NeXT     # Quartus 17.0.x, DE10-Nano / MiSTer
```

Output: `output_files/NeXT.rbf`.

## Boot ROM

Copy `reference/previous/src/Rev_2.5_v66.BIN` to the MiSTer as
`boot1.rom` next to the core (or load it from the OSD, Boot ROM slot).
The machine is held in reset until a ROM is loaded.

## Tests

```
cd tb && ./run_tests.sh            # needs iverilog and python3
```

Runs the real RTL only - the real AP68040 submodule sources, the real
next_* modules, and the real boot ROM image from the Previous
submodule.  See the test list in docs/PORTING.md.
