#!/bin/sh
# NeXT core test suite.
#
# Needs iverilog.  Everything here runs the real RTL: the real AP68040
# core (rtl/AP68040 submodule), the real next_* modules, and the real
# NeXTcube 68040 boot ROM image (Rev 2.5 v66) from the Previous submodule
# (reference/previous).  The only modeled component is the main RAM
# behind the ram_* port, which is DDR3 on hardware.
set -eu
cd "$(dirname "$0")"

RTL=../rtl/next
CPU=../rtl/AP68040/rtl
ROM=../reference/previous/src/Rev_2.5_v66.BIN
WORK=build
mkdir -p "$WORK"

CPUSRC="$CPU/ap040_tg68k_compat.v $CPU/ap040_core.v $CPU/ap040_bus16_adapter.v \
        $CPU/ap040_bus_timeout.v $CPU/ap040_regfile.v $CPU/ap040_alu.v \
        $CPU/ap040_muldiv.v $CPU/ap040_mmu.v $CPU/ap040_cache.v $CPU/ap040_fpu.v \
        $CPU/ap040_walker_cdc.v $RTL/dpram.v"

NEXTSRC="$RTL/next_system.sv $RTL/next_scr.sv $RTL/next_intc.sv \
         $RTL/next_timer.sv $RTL/next_video.sv $RTL/next_vram.sv \
         $RTL/next_rom.sv $RTL/next_bmap.sv $RTL/next_dma_stub.sv \
         $RTL/next_scc.sv $RTL/next_esp.sv $RTL/next_enet_dma.sv $RTL/next_mo.sv \
         $RTL/next_kms_snd.sv $RTL/next_rs.sv"

echo "== converting boot ROM =="
python3 rom2hex.py "$ROM" "$WORK/rom.hex"

echo "== compiling benches =="
iverilog -g2012 -I "$CPU" -o "$WORK/tb_boot.vvp"      tb_next_boot.sv $NEXTSRC $CPUSRC
iverilog -g2012 -o "$WORK/tb_video.vvp"               tb_next_video.sv $RTL/next_video.sv $RTL/next_vram.sv $RTL/dpram.v
iverilog -g2012 -o "$WORK/tb_hardclock.vvp"           tb_next_hardclock.sv $RTL/next_timer.sv $RTL/next_intc.sv
iverilog -g2012 -o "$WORK/tb_rtc.vvp"                 tb_next_rtc.sv $RTL/next_scr.sv
iverilog -g2012 -o "$WORK/tb_scc.vvp"                 tb_next_scc.sv $RTL/next_scc.sv
iverilog -g2012 -o "$WORK/tb_esp.vvp"                 tb_next_esp.sv $RTL/next_esp.sv
iverilog -g2012 -o "$WORK/tb_enet.vvp"                tb_next_enet.sv $RTL/next_enet_dma.sv
iverilog -g2012 -o "$WORK/tb_rs.vvp"                  tb_next_rs.sv $RTL/next_rs.sv
iverilog -g2012 -o "$WORK/tb_mo.vvp"                  tb_next_mo.sv $RTL/next_mo.sv $RTL/next_rs.sv
iverilog -g2012 -o "$WORK/tb_snd.vvp"                 tb_next_snd.sv $RTL/next_kms_snd.sv

echo "== running =="
fail=0
cp rs_vectors.hex "$WORK/.." 2>/dev/null || true

run() {
	name=$1; shift
	echo "--- $name ---"
	if ! vvp "$@" | tee "$WORK/$name.log" | grep -q "ALL PASS"; then
		echo "*** $name FAILED (see tb/$WORK/$name.log)"
		fail=1
	fi
}

run tb_rtc       "$WORK/tb_rtc.vvp"
run tb_scc       "$WORK/tb_scc.vvp"
run tb_esp       "$WORK/tb_esp.vvp"
run tb_enet      "$WORK/tb_enet.vvp"
run tb_rs        "$WORK/tb_rs.vvp"
run tb_mo        "$WORK/tb_mo.vvp"
run tb_snd       "$WORK/tb_snd.vvp"
run tb_hardclock "$WORK/tb_hardclock.vvp"
run tb_video     "$WORK/tb_video.vvp"
run tb_boot      "$WORK/tb_boot.vvp"

if [ "$fail" = 0 ]; then
	echo "== all tests passed =="
else
	echo "== FAILURES =="
	exit 1
fi
