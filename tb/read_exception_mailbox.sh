#!/bin/sh
# Read-only HPS diagnostic capture. No deployment, reset, or memory writes.
# Usage: sh tb/read_exception_mailbox.sh [root@mister] > capture.txt
set -eu
ssh "${1:-root@mister}" '
set -e
for address in \
  0x1ff05000 0x1ff05008 0x1ff05010 0x1ff05018 \
  0x1ff05020 0x1ff05028 0x1ff05030 0x1ff05038 \
  0x1ff05040 0x1ff05048 0x1ff05000 0x1ff05008
do
  devmem "$address" 64
done
'
