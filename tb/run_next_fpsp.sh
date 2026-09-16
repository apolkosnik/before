#!/bin/sh
# Supply the exact NeXT Mach 3.3 sdmach or Improv Mach 2.0 odmach.
set -eu
cd "$(dirname "$0")"
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: sh tb/run_next_fpsp.sh /path/to/kernel [next33|improv]" >&2
    exit 2
fi
profile=${2:-next33}
# Default to the revision each kernel's machine actually reports.  The NeXT
# integration builds AP040_FPU_REVISION=8'h40 (64), matching Previous's
# non-Turbo cube, so that is the configuration a single profile run should
# exercise.  run_fpu_revision_tests.sh sets FPU_REVISION itself and covers
# both revisions regardless of these defaults.
case "$profile" in
    next33) default_revision=64; define_profile= ;;
    improv) default_revision=64; define_profile=-DIMPROV=1 ;;
    *) echo "unknown kernel profile: $profile" >&2; exit 2 ;;
esac
revision=${FPU_REVISION:-$default_revision}
case "$revision" in
    64|65) ;;
    *) echo "FPU_REVISION must be 64 (0x40) or 65 (0x41)" >&2; exit 2 ;;
esac
CPU=${CPU:-../rtl/AP68040/rtl}
WORK=${WORK:-build/next_fpsp_${profile}_rev${revision}}
mkdir -p "$WORK"
python3 next_fpsp_fixture.py "$1" "$WORK/kernel.hex" --profile "$profile"
verilator --binary --timing -j 4 -O3 -Wno-fatal --top-module tb_next_fpsp \
    -GFPU_REVISION="$revision" \
    -I"$CPU" -Mdir "$WORK/vl" -o tb_next_fpsp tb_next_fpsp.sv \
    ../rtl/next/dpram.v \
    "$CPU/ap040_tg68k_compat.v" "$CPU/ap040_core.v" \
    "$CPU/ap040_bus16_adapter.v" "$CPU/ap040_bus_timeout.v" \
    "$CPU/ap040_regfile.v" "$CPU/ap040_alu.v" "$CPU/ap040_muldiv.v" \
    "$CPU/ap040_mmu.v" "$CPU/ap040_cache.v" "$CPU/ap040_fpu.v" \
    "$CPU/ap040_walker_cdc.v" > "$WORK/build.log" 2>&1
for mode in supervisor user; do
    define="$define_profile"
    if [ "$mode" = user ]; then define="$define -DUSERMODE=1"; fi
    if [ "${CHECK_REPLAY:-0}" = 1 ]; then define="$define -DCHECK_REPLAY=1"; fi
    if [ "${CHECK_IMPROV_PACKED:-0}" = 1 ]; then define="$define -DCHECK_IMPROV_PACKED=1"; fi
    "${VASM:-/opt/amiga/bin/vasmm68k_mot}" -m68040 -Fbin -quiet $define \
        -o "$WORK/$mode.bin" next_fpsp.s
    python3 ../rtl/AP68040/tb/bin2hex.py "$WORK/$mode.bin" "$WORK/$mode.hex"
    for latency in 0 3; do
        for fill in 0000 a55a; do
            echo "== $profile rev=$revision $mode FPSP, memory latency $latency, stack fill $fill =="
            "$WORK/vl/tb_next_fpsp" +prog="$WORK/$mode.hex" \
                +kernel="$WORK/kernel.hex" +latency="$latency" +stackfill="$fill"
        done
    done
done

# Optional exact-libc regression for Workspace's default screen dimensions.
if [ -n "${LIBSYS:-}" ]; then
    if [ "$profile" != improv ]; then
        echo "LIBSYS fixture requires the Improv kernel" >&2
        exit 2
    fi
    python3 next_libsys_fixture.py "$LIBSYS" "$WORK"
    "${VASM:-/opt/amiga/bin/vasmm68k_mot}" -m68040 -Fbin -no-opt -quiet \
        -o "$WORK/sscanf.bin" next_sscanf.s
    python3 ../rtl/AP68040/tb/bin2hex.py "$WORK/sscanf.bin" "$WORK/sscanf.hex"
    for latency in 0 3; do
        for fill in 0000 a55a; do
            echo "== Improv libc sscanf rev=$revision, latency $latency, fill $fill =="
            "$WORK/vl/tb_next_fpsp" +prog="$WORK/sscanf.hex" \
                +kernel="$WORK/kernel.hex" +libtext="$WORK/libtext.hex" \
                +libdata="$WORK/libdata.hex" +latency="$latency" +stackfill="$fill"
        done
    done
fi
