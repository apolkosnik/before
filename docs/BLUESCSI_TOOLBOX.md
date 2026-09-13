# BlueSCSI Toolbox enablement investigation

Investigated September 12, 2026 against NeXT commit `04bb757` and the local
Main_MiSTer checkout at `ce39817`.

**This needs an implementation in both the NeXT core and Main_MiSTer, plus
a native NeXT client. There is no existing setting that enables it.**
The useful shortcut is the file-transfer and CD-changer backend already in
`/home/adam/MiSTer_Main/Main_MiSTer/support/mac/mac_toolbox.cpp`.
That backend is currently connected only to the Mac core family.

## What exists today

| Component | Finding |
| --- | --- |
| NeXT SCSI target | Implements disk commands, but none of the Toolbox vendor commands. Unknown opcodes return CHECK CONDITION. |
| Command framing | `cdb_length()` treats group 6, including `0xD0`–`0xDA`, as six bytes. Toolbox commands need ten. |
| Discovery | INQUIRY identifies the target as Previous; its extended data contains no Toolbox signature/API version. MODE SENSE page `0x31` is unsupported. |
| File access | The FPGA has mounted-image sector access through `hps_io`; directory listing and ordinary host files need an ARM-side handler. |
| CD changes | OSD slot 3 mounts ISO images. START STOP UNIT currently succeeds without ejecting anything. Mount pulses clear sense rather than reporting media change. |
| Ejected drive | Selection currently requires `disk_present_v`. A changer must keep the CD target selectable with its tray empty so another image can be selected. |

The relevant code is in [next_scsi.sv](../rtl/next/next_scsi.sv):
`cdb_length`, `inq_byte`, `page_len`, `cmd_lun`, the selection handler,
`X_DISPATCH`, and the mount/sense handlers. The slot routing is in
[NeXT.sv](../NeXT.sv).

## Protocol to support

BlueSCSI documents a vendor-command interface with this minimum operation
set. These commands travel through the normal SCSI command/data/status
phases; they do not require a physical BlueSCSI board.

| Function | Commands |
| --- | --- |
| Discover support | INQUIRY `0x12`, MODE SENSE `0x1A` page `0x31`, API/capability discovery |
| List and download files | COUNT `0xD2`, LIST `0xD0`, GET `0xD1` |
| Upload files | SEND PREP `0xD3`, SEND DATA `0xD4`, SEND END `0xD5` |
| List and change CDs | COUNT CDS `0xDA`, LIST CDS `0xD7`, SET NEXT CD `0xD8` |
| Discover targets/features | Metadata `0xD9`, initially list-devices and supported-capability queries |

Discovery must describe only implemented features. Directory changing,
firmware updating, and Wi-Fi are outside this proposal. See the
[Toolbox developer documentation](https://bluescsi.com/docs/Toolbox-Developer-Docs).

Details checked against the firmware source:

- GET uses a file index and an offset measured in 4,096-byte blocks. A
  legacy client requests one block; the last response can be short.
- SEND PREP receives a filename; SEND DATA has a separate byte count and
  an offset field measured in 512-byte units. Test multi-block uploads
  against the chosen client: the inspected firmware uses `seekCur`, while
  the local Main backend uses absolute `fseeko` for this field.
- LIST entries are 40 bytes, including a 32-character name plus NUL and a
  five-byte size. The firmware encodes **file = 1, directory = 0**; the
  documentation's struct comment reverses those meanings.
- CDB byte 1 contains the full file/image index for GET and SET NEXT CD.
  NeXT's fallback `cdb1[7:5]` LUN extraction must not interpret indices
  32 and above as another LUN when selection has no IDENTIFY message.
- BlueSCSI appends its identification/API data after the standard
  36-byte INQUIRY response. Simply replacing the eight-byte vendor name
  is insufficient for clients that validate extended discovery data.

Sources: [Toolbox implementation](https://github.com/BlueSCSI/BlueSCSI-v2/blob/main/src/BlueSCSI_Toolbox.cpp),
[INQUIRY implementation](https://github.com/BlueSCSI/BlueSCSI-v2/blob/main/lib/SCSI2SD/src/firmware/inquiry.c),
[vendor mode page](https://github.com/BlueSCSI/BlueSCSI-v2/blob/main/lib/SCSI2SD/src/firmware/mode.c).

## Recommended transport and host changes

Use one additional **host transport slot 6**, shared by file and CD
commands, with the opcode choosing the backend. This is a proposed layout,
not support present in the released RBF.

| Host slot | Purpose |
| --- | --- |
| 0–2 | Existing SCSI disks |
| 3 | Existing SCSI CD image |
| 4 | Existing floppy image |
| 5 | Existing magneto-optical image |
| 6 | New Toolbox request/response transport |

Raise `VDNUM` from 6 to 7. Slot 6 is a transport endpoint, not a new guest
SCSI target: route its requests and acknowledgements separately from
`t_unit` and the target arrays. Do not copy the Mac backend's slot numbers
3 and 5; those are already real NeXT drives. Slot 7 is unavailable under
the current mount protocol because its bit overlaps the read-only flag.

The local Mac transport already offers CDB/payload request blocks, a status
block with signature `0xB5` and response length, and streamed response
sectors. Reuse this structure and the host file operations through common
helpers or NeXT-specific adapters. Add `is_next()`-gated sector service,
polling, and session cleanup in Main. A matching Main build is required.

Suggested folders are `games/NeXT/shared` and `games/NeXT/CD3`; the existing
`shared_folder` configuration can override the former. Restrict the
initial CD listing to ISO, which the NeXT path already serves. The Mac
backend also lists CUE/CHD/raw images, but its format translation hooks are
Mac-specific and are not automatically available to NeXT.

The FPGA can stream through its 512-byte SCSI buffer instead of allocating
a whole file buffer. Preserve the exact payload length and DMA residuals.
Start with legacy 4 KiB reads and 512-byte uploads; advertise larger
transfers only after testing their length handling. Backend absence,
failure, reset, or a stale reply must terminate with a defined error.

Before reuse, adapt the host backend's per-session state, file-size limits,
directory encoding, and error reporting. Keep count/list/index resolution
consistent; confine file operations to the shared directory, including
symlink handling. Preserve buffered-write and close errors on uploads.

## NeXT client and CD lifecycle

The official documentation still puts NeXT under **In Progress** and links
to the author's development thread. The thread reports working CD-changing
prototypes and difficulty coordinating with Workspace Manager; its latest
visible author update says the project remains on the to-do list. I did
not find a confirmed public NeXT release providing both requested features.
See the [platform documentation](https://bluescsi.com/docs/Toolbox) and
[author's updates](https://tinkerdifferent.com/threads/bluescsi-toolbox-next-edition.3536/page-2).

The practical first client is a native NeXT CLI with list/get/put/list-CD/
switch-CD operations. [SonnyJim/bstoolbox](https://github.com/SonnyJim/bstoolbox)
has these operations and an OS abstraction, but its inspected Makefile
supports Linux and IRIX only. It would need a NeXT backend and compiler
compatibility work; it is not an existing NeXT binary.

NeXT already exposes raw commands through `/dev/sgN`: bind target/LUN with
`SGIOCSTL`, then issue `struct scsi_req` via `SGIOCREQ`. Check the returned
I/O status, SCSI status, and actual DMA byte count, not just `ioctl()`'s
return value. This supplies the required guest interface without a new
kernel driver. See NeXT's [sg(4) manual](https://www.typewritten.org/Manual/NeXT/NEXTSTEP/3.3/man4/sg.html).

CD switching must coordinate guest unmount/eject, host image replacement,
and guest insertion detection. Implement separate drive-present and
medium-present state, START STOP UNIT eject/load, and appropriate NOT READY
and UNIT ATTENTION/REQUEST SENSE handling. Check whether the guest also
uses PREVENT/ALLOW MEDIUM REMOVAL. Keep the current 512-byte guest block
view of ISO data, which the NeXT boot/install path relies on.

Defer the host remount to the Main poll loop as the Mac backend does, but
complete it under the new NeXT media lifecycle. A backend command merely
being staged does not prove the new image opened successfully. Preserve
the old image on open failure and report that failure to the guest.

For initial testing, have Workspace Manager eject the old CD before the
client switches it. Automating that step needs testing on NeXTSTEP 3.3:
`disk -e` alone is insufficient because it does not unmount filesystems,
according to NeXT's [disk(8) manual](https://www.typewritten.org/Manual/NeXT/NEXTSTEP/3.3/man8/disk.html).

## Implementation and validation order

1. Add discovery, ten-byte vendor CDB framing, slot 6 transport, and host
   count/list/get handling. Exercise it with a small NeXT CLI.
2. Add uploads and verify byte-for-byte round trips, including empty files,
   1/511/512/513/4095/4096/4097-byte files, and indices 31/32/99.
3. Add ISO listing/switching and the removable-media lifecycle. Verify
   changed capacity and contents, eject/reinsert, failed mounts, and
   Workspace Manager recognition without rebooting.
4. Exercise PIO and DMA, partial final transfers, backend absence, reset
   during transfers, stale replies, and isolation from disk/floppy/MO I/O.
   Run the existing device/boot regressions and the Quartus timing gate.

This investigation changed no RTL, Main code, RBF, or live MiSTer state.
Downloaded reference sources are under `tb/build/toolbox_investigation/`.
