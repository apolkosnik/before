#!/bin/sh
# Diagnostic audit: exits 1 when any required-behavior probe reports FAIL.
set -eu
cd "$(dirname "$0")/.."
work=tb/build/audio_audit
mkdir -p "$work"
build() {
    name=$1
    shift
    verilator --binary --timing -j 4 -Wno-fatal --top-module "audit_audio_$name" \
        -Mdir "$work/$name" -o "audit_audio_$name" \
        "tb/audit_audio_$name.sv" "$@" > "$work/${name}_build.log" 2>&1
}
build playback rtl/next/next_kms_snd.sv
build capture rtl/next/next_audio_adc.sv rtl/next/next_snd_in.sv sys/ltc2308.sv
build mister sys/audio_out.sv sys/iir_filter.v sys/i2s.v sys/spdif.v sys/sigma_delta_dac.v
for name in playback capture mister; do
    "$work/$name/audit_audio_$name" > "$work/$name.log"
    cat "$work/$name.log"
done
python3 tb/analyze_audio_audit.py > "$work/analysis.log"
cat "$work/analysis.log"
if rg -q '^FAIL:' "$work/playback.log" "$work/capture.log" "$work/mister.log"; then
    exit 1
fi
