#!/bin/sh
# Exercise the CPU cache with both its standalone and the NeXT tag RAM model.
set -eu
cd "$(dirname "$0")"
mkdir -p build
CPU=../rtl/AP68040
for model in standalone next; do
	if [ "$model" = standalone ]; then
		ram="$CPU/rtl/primitives/dpram.v"
	else
		ram=../rtl/next/dpram.v
	fi
	iverilog -g2012 -I "$CPU/rtl" -s tb_ap040_cache_snoop \
		-o "build/cache_snoop_$model.vvp" "$CPU/tb/tb_ap040_cache_snoop.v" \
		"$CPU/rtl/ap040_cache.v" "$ram"
	vvp "build/cache_snoop_$model.vvp" > "build/cache_snoop_$model.log"
	if ! grep -q 'ALL TESTS PASSED' "build/cache_snoop_$model.log"; then
		sed -n '1,100p' "build/cache_snoop_$model.log"
		exit 1
	fi
	printf 'PASS: cache snoops with %s RAM\n' "$model"
done
