#!/bin/sh
# A separately supplied NeXT Mach 3.3 RELEASE_M68K sdmach is required.
set -eu
cd "$(dirname "$0")"
if [ "$#" != 1 ]; then
    echo "usage: sh tb/run_next_fpsp.sh /path/to/sdmach" >&2
    exit 2
fi
CPU=${CPU:-../rtl/AP68040/rtl}
WORK=${WORK:-build/next_fpsp}
mkdir -p "$WORK"
python3 next_fpsp_fixture.py "$1" "$WORK/kernel.hex"
verilator --binary --timing -j 4 -O3 -Wno-fatal --top-module tb_next_fpsp \
    -I"$CPU" -Mdir "$WORK/vl" -o tb_next_fpsp tb_next_fpsp.sv \
    ../rtl/next/dpram.v \
    "$CPU/ap040_tg68k_compat.v" "$CPU/ap040_core.v" \
    "$CPU/ap040_bus16_adapter.v" "$CPU/ap040_bus_timeout.v" \
    "$CPU/ap040_regfile.v" "$CPU/ap040_alu.v" "$CPU/ap040_muldiv.v" \
    "$CPU/ap040_mmu.v" "$CPU/ap040_cache.v" "$CPU/ap040_fpu.v" \
    "$CPU/ap040_walker_cdc.v" > "$WORK/build.log" 2>&1
for mode in supervisor user; do
    define=""
    if [ "$mode" = user ]; then define="-DUSERMODE=1"; fi
    if [ "${CHECK_REPLAY:-0}" = 1 ]; then define="$define -DCHECK_REPLAY=1"; fi
    "${VASM:-/opt/amiga/bin/vasmm68k_mot}" -m68040 -Fbin -quiet $define \
        -o "$WORK/$mode.bin" next_fpsp.s
    python3 ../rtl/AP68040/tb/bin2hex.py "$WORK/$mode.bin" "$WORK/$mode.hex"
    for latency in 0 3; do
        for fill in 0000 a55a; do
            echo "== $mode FPSP, memory latency $latency, stack fill $fill =="
            "$WORK/vl/tb_next_fpsp" +prog="$WORK/$mode.hex" \
                +kernel="$WORK/kernel.hex" +latency="$latency" +stackfill="$fill"
        done
    done
done
