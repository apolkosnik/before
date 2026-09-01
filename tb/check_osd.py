#!/usr/bin/env python3
#
#  The OSD image slots against what the host can actually address.
#
#  None of the benches instantiate hps_io: they drive img_mounted and
#  the sd_* signals directly, so the wiring between the menu and the
#  host is the one part of the core no simulation covers.  Every fault
#  it has produced was found on hardware, and the worst of them was
#  silent - a mount arriving at the wrong device rather than not at
#  all.
#
#  The host announces a mount in a single byte:
#
#      spi_uio_cmd8(UIO_SET_SDSTAT, (1 << slot) | (read only ? 0x80))
#
#  and hps_io reads bit 7 of that byte as the read-only flag:
#
#      img_mounted  <= io_din[VD:0] ? io_din[VD:0] : 1'b1;
#      img_readonly <= io_din[7];
#
#  So slot 7 collides with the flag, and slots 8 and above shift clean
#  out of the byte, leaving a mask of zero - which hps_io reads as "no
#  slot given" and turns into slot 0.  An image mounted at S8 therefore
#  arrives as a mount of whatever device owns slot 0, carrying the
#  wrong size.  Seven slots is the ceiling, and it is the host that
#  sets it, not VDNUM.
#
#  The menu also carries its own slot numbers, so the order it lists
#  them in need not match the numbering.  That is useful - it lets the
#  devices read in device order while a saved configuration keeps its
#  mounts - but it means the routing below cannot be checked by eye
#  against the menu above.  Hence this.

import re
import sys

TOP = "../NeXT.sv"
MAX_SLOTS = 7          # bit 7 is the read-only flag; 8 and up fall out


def fail(msg):
    print(f"*** OSD slot check: {msg}")
    sys.exit(1)


src = open(TOP).read()

# VDNUM as given to hps_io
m = re.search(r"hps_io\s*#\s*\(.*?\.VDNUM\(\s*(\d+)\s*\)", src, re.S)
if not m:
    fail("cannot find the hps_io VDNUM parameter")
vdnum = int(m.group(1))

# the menu's own slot entries, "S<n>,<extensions>,<label>;"
entries = re.findall(r'"S(\d),([^,]*),([^;"]*);"', src)
if not entries:
    fail("no S entries found in CONF_STR")
slots = [(int(n), label) for n, _, label in entries]

# The host reads the extension list in three character groups, padding
# the whole string out with spaces to reach a multiple of three.  A two
# letter extension therefore carries its own trailing space, and one
# written without it swallows the first letter of the next - or falls
# off the end.  A file whose extension is missing from the list is not
# hidden with a warning; it simply does not appear in the browser, and
# the drive looks empty for a reason nothing on the machine can show.
NATIVE = [("floppy", "FD "), ("optical", "OD ")]

for n, ext, label in entries:
    if len(ext) % 3:
        fail(f'S{n} ("{label.strip()}") lists extensions "{ext}", which is '
             f"not a whole number of three character groups; the host pads "
             f"the end and every group after the short one shifts")
    groups = [ext[i:i + 3] for i in range(0, len(ext), 3)]
    for kind, want in NATIVE:
        if kind in label.lower() and want not in groups:
            fail(f'S{n} ("{label.strip()}") is the {kind} but its extensions '
                 f'{groups} do not include "{want}" - a NeXT {kind} image is '
                 f'.{want.strip().lower()} and would not be listed at all')

nums = [n for n, _ in slots]

if vdnum > MAX_SLOTS:
    fail(f"VDNUM is {vdnum}; the host announces a mount in one byte whose "
         f"bit 7 is the read-only flag, so slots above {MAX_SLOTS - 1} "
         f"cannot be addressed - a mount there silently lands on slot 0")

for n, label in slots:
    if n >= vdnum:
        fail(f'slot S{n} ("{label.strip()}") is outside VDNUM={vdnum}; '
             f"hps_io never raises its mount bit")

dupes = {n for n in nums if nums.count(n) > 1}
if dupes:
    fail(f"slot number(s) {sorted(dupes)} used by more than one device")

missing = sorted(set(range(vdnum)) - set(nums))
if missing:
    fail(f"VDNUM={vdnum} but slot(s) {missing} have no menu entry")

# The unpacked arrays handed to hps_io are indexed 0..VDNUM-1, while the
# packed sd_rd/sd_wr vectors run the other way.  Getting one of them a
# device short does not fail to build; it crosses two drives over.
for port in ("sd_lba", "sd_buff_din"):
    m = re.search(r"\.%s\('\{(.*?)\}\)" % port, src, re.S)
    if not m:
        fail(f"cannot find the {port} array handed to hps_io")
    n = len([e for e in m.group(1).split(",") if e.strip()])
    if n != vdnum:
        fail(f"{port} lists {n} entries for VDNUM={vdnum}")

# Which slots each device's mount signal is actually built from.  The
# menu carries its own numbers and may list them in any order, so a
# relabelled entry and the routing under it can disagree without
# anything failing to build - the mount simply arrives at the wrong
# device, which is how a floppy image once turned up as a disk.
DEVICES = [
    ("img_mounted",  "disk",    ("disk",)),
    ("fimg_mounted", "floppy",  ("floppy",)),
    ("oimg_mounted", "optical", ("optical",)),
]

for wire, kind, words in DEVICES:
    m = re.search(r"wire\s*\[[^\]]*\]\s*%s\s*=([^;]*);" % wire, src)
    if not m:
        continue                      # a device this core does not have
    routed = set()
    for a, b in re.findall(r"img_mounted_v\[(\d+)(?::(\d+))?\]", m.group(1)):
        routed |= set(range(int(b), int(a) + 1)) if b else {int(a)}

    labelled = {n for n, label in slots
                if any(w in label.lower() for w in words)}

    if routed != labelled:
        fail(f"{kind}: the menu labels slot(s) "
             f"{sorted(labelled) or 'none'} but {wire} is built from "
             f"{sorted(routed) or 'none'} - a mount would arrive at the "
             f"wrong device")

print(f"== OSD slots: {vdnum} within the host's {MAX_SLOTS}, "
      f"{', '.join('S%d' % n for n in sorted(nums))} each routed to the "
      f"device its label names ==")
