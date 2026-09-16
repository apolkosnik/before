#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
cpu=../rtl/AP68040/rtl
for diag in 0 1; do
	verilator --binary --timing -j 4 -O3 -Wno-fatal --top-module tb_next_exception_cpu \
		-GDIAGNOSTIC="$diag" -Mdir "build/vl_tb_next_exception_cpu_$diag" \
		-o tb_next_exception_cpu -I"$cpu" \
		tb_next_exception_cpu.sv "$cpu"/*.v ../rtl/next/dpram.v \
		> "build/vl_tb_next_exception_cpu_$diag.log" 2>&1
	"build/vl_tb_next_exception_cpu_$diag/tb_next_exception_cpu"
done
for diag in 0 2; do
	verilator --binary --timing -j 4 -O3 -Wno-fatal --top-module tb_next_unhandled_cpu \
		-GDIAGNOSTIC="$diag" -Mdir "build/vl_tb_next_unhandled_cpu_$diag" \
		-o tb_next_unhandled_cpu -I"$cpu" \
		tb_next_unhandled_cpu.sv "$cpu"/*.v ../rtl/next/dpram.v \
		../rtl/next/next_exception_trigger.sv ../rtl/next/next_exception_mailbox.sv \
		> "build/vl_tb_next_unhandled_cpu_$diag.log" 2>&1
	"build/vl_tb_next_unhandled_cpu_$diag/tb_next_unhandled_cpu"
done
verilator --binary --timing -j 4 -O3 -Wno-fatal --top-module tb_next_exception_trigger \
	-Mdir build/vl_tb_next_exception_trigger -o tb_next_exception_trigger \
	tb_next_exception_trigger.sv ../rtl/next/next_exception_trigger.sv \
	> build/vl_tb_next_exception_trigger.log 2>&1
build/vl_tb_next_exception_trigger/tb_next_exception_trigger
verilator --binary --timing -j 4 -O3 -Wno-fatal --top-module tb_next_exception_mailbox \
	-Mdir build/vl_tb_next_exception_mailbox -o tb_next_exception_mailbox \
	tb_next_exception_mailbox.sv ../rtl/next/next_exception_mailbox.sv \
	../rtl/next/next_ddram_arb.sv > build/vl_tb_next_exception_mailbox.log 2>&1
build/vl_tb_next_exception_mailbox/tb_next_exception_mailbox
