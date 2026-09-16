#!/bin/sh
# Frame-revision regression matrix. Kernels are external, hash-validated inputs.
set -eu
cd "$(dirname "$0")"
if [ "$#" != 2 ]; then
    echo "usage: sh tb/run_fpu_revision_tests.sh /absolute/sdmach /absolute/odmach" >&2
    exit 2
fi
work=${WORK:-build/fpu_revision_tests}
mkdir -p "$work"
work=$(cd "$work" && pwd)
for revision in 64 65; do
    FPU_REVISION="$revision" WORK="$work/next33_$revision" \
        sh ./run_next_fpsp.sh "$1" next33 > "$work/next33_$revision.log" 2>&1
    echo "PASS NeXT 3.3 FPSP revision=$revision (eight configurations)"
done
FPU_REVISION=64 WORK="$work/improv_64" sh ./run_next_fpsp.sh "$2" improv \
    > "$work/improv_64.log" 2>&1
echo "PASS Improv frame regression revision=64 (eight configurations; packed input excluded)"
VASM=${VASM:-/opt/amiga/bin/vasmm68k_mot}
"$VASM" -m68040 -Fbin -quiet -DIMPROV=1 -DUSERMODE=1 -DIMPROV_TRACE_ONLY=1 \
    -o "$work/trace.bin" next_fpsp.s
python3 ../rtl/AP68040/tb/bin2hex.py "$work/trace.bin" "$work/trace.hex"
for latency in 0 3; do
    for fill in 0000 a55a; do
        for revision in 64 65; do
            log="$work/trace_${revision}_${latency}_${fill}.log"
            result=0
            "$work/next33_$revision/vl/tb_next_fpsp" +prog="$work/trace.hex" \
                +kernel="$work/improv_64/kernel.hex" +latency="$latency" \
                +stackfill="$fill" +trace +frames > "$log" 2>&1 || result=$?
            if [ "$revision" = 64 ]; then
                test "$result" = 0
                grep -q 'ALL PASS: real NeXT FPSP' "$log"
            else
                test "$result" != 0
                grep -q 'EXC stage=12 vec=11 .*sr=0000' "$log"
                # Wrong UNIMP layout corrupts the old handler's operand.
                # BUSY resume now consumes its nested correction, so the
                # malformed outer restore fails before the old trace branch.
                grep -q 'EXC stage=12 vec=55 pc=04080b36 ' "$log"
                grep -q 'RESTORE stage=12 pc=04080416 .*busy=1 cmd=4800 ' "$log"
                grep -q 'CU_SAVEPC=fe ' "$log"
                grep -q 'EXC stage=12 vec=14 pc=0408030a ' "$log"
                grep -q 'FAIL stage=12 ' "$log"
                if grep -q 'EXC .*vec=9 ' "$log"; then exit 1; fi
            fi
        done
        echo "PASS trace regression latency=$latency fill=$fill (0x40 works; 0x41 fails as required)"
    done
done
