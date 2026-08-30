#!/bin/sh
# NeXT core test suite, run with Verilator.
#
# Everything here runs the real RTL: the real AP68040 core (rtl/AP68040
# submodule), the real next_* modules, and the real NeXTcube 68040 boot
# ROM image (Rev 2.5 v66) from the Previous submodule
# (reference/previous).  The boot bench runs the real next_ddram and
# next_ddram_arb, and the real mailbox bridge on the arbiter's port B,
# against a DDR3 model that throttles and starts the mailbox as
# rubbish: the only modeled component is the DDR3 device itself.
#
#   ./run_tests.sh         device suites plus a 3 ms boot smoke test
#   ./run_tests.sh bootsd  additionally boots with a mounted disk image
#                          and checks the ROM's SCSI boot path
#   ./run_tests.sh post    additionally runs the full power-on system
#                          test to the "System test passed" path
#                          (about 5 seconds of machine time)
set -eu
cd "$(dirname "$0")"

RTL=../rtl/next
CPU=../rtl/AP68040/rtl
ROM=../reference/previous/src/Rev_2.5_v66.BIN
WORK=build
mkdir -p "$WORK"

VFLAGS="--binary --timing -j 4 -O3 -Wno-fatal"

CPUSRC="$CPU/ap040_tg68k_compat.v $CPU/ap040_core.v $CPU/ap040_bus16_adapter.v \
        $CPU/ap040_bus_timeout.v $CPU/ap040_regfile.v $CPU/ap040_alu.v \
        $CPU/ap040_muldiv.v $CPU/ap040_mmu.v $CPU/ap040_cache.v $CPU/ap040_fpu.v \
        $CPU/ap040_walker_cdc.v"

NEXTSRC="$RTL/next_system.sv $RTL/next_scr.sv $RTL/next_intc.sv \
         $RTL/next_timer.sv $RTL/next_video.sv $RTL/next_vram.sv \
         $RTL/next_rom.sv $RTL/next_bmap.sv $RTL/next_dma_stub.sv \
         $RTL/next_scc.sv $RTL/next_scsi.sv $RTL/next_enet_dma.sv \
         $RTL/next_mo.sv $RTL/next_kms_snd.sv $RTL/next_rs.sv \
         $RTL/next_floppy.sv $RTL/next_ddram.sv $RTL/next_ddram_arb.sv \
         $RTL/next_enet_bridge.sv $RTL/dpram.v"

echo "== converting boot ROM =="
python3 rom2hex.py "$ROM" "$WORK/rom.hex"

vbuild() {
	top=$1; shift
	verilator $VFLAGS --top-module "$top" -Mdir "$WORK/vl_$top" \
		-o "$top" "$@" > "$WORK/vl_$top.log" 2>&1 || {
		echo "*** verilation of $top FAILED (see tb/$WORK/vl_$top.log)"
		exit 1
	}
}

echo "== verilating benches =="
vbuild tb_next_rtc       tb_next_rtc.sv $RTL/next_scr.sv
vbuild tb_next_scc       tb_next_scc.sv $RTL/next_scc.sv
vbuild tb_next_esp       tb_next_esp.sv $RTL/next_scsi.sv
vbuild tb_next_floppy    tb_next_floppy.sv $RTL/next_floppy.sv
vbuild tb_next_flpdma    tb_next_flpdma.sv $RTL/next_floppy.sv $RTL/next_scsi.sv
vbuild tb_next_scsi      tb_next_scsi.sv $RTL/next_scsi.sv
vbuild tb_next_enet      tb_next_enet.sv $RTL/next_enet_dma.sv
vbuild tb_next_bridge    tb_next_bridge.sv $RTL/next_enet_dma.sv $RTL/next_enet_bridge.sv
vbuild tb_next_ddram_arb tb_next_ddram_arb.sv $RTL/next_ddram_arb.sv
vbuild tb_next_rs        tb_next_rs.sv $RTL/next_rs.sv
vbuild tb_next_mo        tb_next_mo.sv $RTL/next_mo.sv $RTL/next_rs.sv
vbuild tb_next_snd       tb_next_snd.sv $RTL/next_kms_snd.sv
vbuild tb_next_kbd       tb_next_kbd.sv $RTL/next_kms_snd.sv
vbuild tb_next_hardclock tb_next_hardclock.sv $RTL/next_timer.sv $RTL/next_intc.sv
vbuild tb_next_video     tb_next_video.sv $RTL/next_video.sv $RTL/next_vram.sv $RTL/dpram.v
vbuild tb_next_boot      -I"$CPU" tb_next_boot.sv $NEXTSRC $CPUSRC

# The emu top is only ever compiled by Quartus, so a duplicate
# declaration or a mistyped port there costs a forty minute build to
# discover.  Lint it here: module resolution is not the point, the
# file's own declarations are, so MODMISSING is filtered out.
echo "== linting the emu top =="
if verilator --lint-only -Wno-fatal -I.. -I../sys --top-module emu ../NeXT.sv $NEXTSRC 2>&1 \
     | grep -E "^%Error" | grep -vE "MODMISSING|Exiting due to" | grep -q .; then
	echo "*** NeXT.sv lint FAILED"
	verilator --lint-only -Wno-fatal -I.. -I../sys --top-module emu ../NeXT.sv $NEXTSRC 2>&1 \
		| grep -E "^%Error" | grep -vE "MODMISSING|Exiting due to" | head -5
	fail=1
fi

echo "== running =="
fail=0
run() {
	name=$1; shift
	echo "--- $name ---"
	if ! "$@" | tee "$WORK/$name.log" | grep -q "ALL PASS"; then
		echo "*** $name FAILED (see tb/$WORK/$name.log)"
		fail=1
	fi
}

run tb_rtc       "$WORK/vl_tb_next_rtc/tb_next_rtc"
run tb_scc       "$WORK/vl_tb_next_scc/tb_next_scc"
run tb_esp       "$WORK/vl_tb_next_esp/tb_next_esp"
run tb_scsi      "$WORK/vl_tb_next_scsi/tb_next_scsi"
run tb_floppy    "$WORK/vl_tb_next_floppy/tb_next_floppy"
run tb_flpdma    "$WORK/vl_tb_next_flpdma/tb_next_flpdma"
run tb_enet      "$WORK/vl_tb_next_enet/tb_next_enet"
run tb_bridge    "$WORK/vl_tb_next_bridge/tb_next_bridge"
run tb_ddram_arb "$WORK/vl_tb_next_ddram_arb/tb_next_ddram_arb"
run tb_rs        "$WORK/vl_tb_next_rs/tb_next_rs"
run tb_mo        "$WORK/vl_tb_next_mo/tb_next_mo"
run tb_snd       "$WORK/vl_tb_next_snd/tb_next_snd"
run tb_kbd       "$WORK/vl_tb_next_kbd/tb_next_kbd"
run tb_hardclock "$WORK/vl_tb_next_hardclock/tb_next_hardclock"
run tb_video     "$WORK/vl_tb_next_video/tb_next_video"
run tb_boot      "$WORK/vl_tb_next_boot/tb_next_boot"
run tb_boot_noet "$WORK/vl_tb_next_boot/tb_next_boot" +netoff

if [ "${1:-}" = "post" ]; then
	echo "--- full power-on system test (about 5 minutes) ---"
	# The memory model is the machine's full 64 MB, and the ROM clears
	# all of it before the tests: the success marker lands near 900
	# million cycles, so a 500 million budget times out on a POST that
	# is running perfectly well.
	if ! "$WORK/vl_tb_next_boot/tb_next_boot" +mcycles=1400 \
		| tee "$WORK/tb_post.log" | grep -q "system test passed path"; then
		echo "*** full POST FAILED (see tb/$WORK/tb_post.log)"
		fail=1
	else
		grep -E "measured|passed path" "$WORK/tb_post.log" | tail -2
	fi
fi

if [ "${1:-}" = "bootsd" ]; then
	echo "--- SCSI boot path: POST plus disk boot (about 7 minutes) ---"
	if ! "$WORK/vl_tb_next_boot/tb_next_boot" +bootsd +mcycles=1600 \
		| tee "$WORK/tb_bootsd.log" | grep -q "boot: ROM selected the SCSI disk"; then
		echo "*** SCSI boot path FAILED (see tb/$WORK/tb_bootsd.log)"
		fail=1
	else
		grep -E "passed path|BOOT:|boot:" "$WORK/tb_bootsd.log" | head -12
	fi
fi

if [ "$fail" = 0 ]; then
	echo "== all tests passed =="
else
	echo "== FAILURES =="
	exit 1
fi
