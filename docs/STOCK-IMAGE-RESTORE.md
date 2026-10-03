# Six-LUN stock image restoration

This workflow restores the six capacity-derived Uke stock GPTs and explicitly
selected, hash-pinned OS image programming extents. It prepares and verifies
every changed range's complete original and replacement before any target
write. The native CLI and OrangeFox pages share the same engine.

**Execution currently accepts six different regular images only.** Pad 7 and
POCO Pad X1 selections and SKU tags are declarations, not accepted hardware
profiles. Live UFS writes, installed firmware/model/SKU verification and
physical rollback acceptance remain pending. The supported source profile is
Global OS3.0.303.0.WOZMIXM; CN and other versions need independent reviewed pins.

## Source and destination contract

The host catalog script verifies the complete OEM archive, extracts a new
derived data set and hashes ten selected OS images and thirty GPT/XML inputs.
It does not execute OEM programs or acquire early firmware/calibration images.
Source archives remain under the component's own reference directory.

The native decoder verifies Android sparse v1 structure, every declared CRC
and exact source/output boundaries. It binds a reviewed decoded-content digest
before staging. RAW, FILL, DONT_CARE and CRC32 chunks are supported; unsupported
versions, unknown chunks, overflow, truncation and trailing bytes are refused.
The upstream format and zero treatment used for checksum verification are
documented in [AOSP libsparse](https://android.googlesource.com/platform/system/core/+/5fa5708025843fe24566401025d830f05a3a39e2/libsparse/sparse_read.cpp).

Source bytes, decoded programming bytes and destination capacity are distinct.
The pinned DTBO file is 20 MiB; its stock partition is 24 MiB. The remaining
4 MiB stays protected. The sparse userdata image decodes to exactly 45 GiB.
This is its programming extent, not a measurement of the tablet's capacity.

Only these destinations are supported:

| OEM file | LUN | Explicit destinations |
|---|---:|---|
| boot, dtbo, init_boot, recovery, vendor_boot, vbmeta | 4 | Corresponding A/B labels |
| vbmeta_system | 0 | vbmeta_system_a / vbmeta_system_b |
| super | 0 | super |
| metadata / userdata | 0 | Paired Android data reset |

Labels, LUNs and filenames must match compiled pins. Metadata and userdata
require a paired data-loss choice. Sparse DONT_CARE ranges require explicit
zero policy and are zeroed inside the reviewed programming extent. A larger
userdata tail stays protected: this is not secure erasure or proof of Android
FBE/boot compatibility. Slot destinations never switch the active boot slot.

## Journal and recovery

The journal needs two complete copies of every programming extent plus a
64 MiB margin. Sparse/reflink staging can reduce actual allocation but does not
reduce that conservative free-space requirement. Keep persistent storage;
memory-backed journals such as /tmp disappear on reboot.

All six image descriptors and the journal are locked for cooperating processes.
Path, inode, capacity, sector size, owner and permissions stay bound. Hardlinks,
duplicate image identities and live loop aliases are refused. The journal has
mode 0700; its records and mirrors are owned single-link mode-0600 files.

Every byte outside the disjoint payload/GPT programming extents is protected.
This includes unselected slot contents, early firmware, calibration, unit-bound
data and unprogrammed tails. Stock table restoration can remove custom OS
partition visibility; those bytes remain unless an explicit selected payload
overwrites their reviewed extent. GUIDs come from current original GPTs or a
verified same-target original backup, never from generic OEM template identities.

The digest named **ure-image-range-sha256-tree-v1** uses domain separation,
lengths and ordinary SHA-256 leaves of at most 4 MiB. It is not an ordinary
whole-file SHA-256 checksum. Allocated zeros and kernel-reported holes have
identical logical-content digests. Only regular-file holes are optimized;
this never establishes that existing UFS bytes are zero. Target writes are
skipped only when an independent read equals the reviewed replacement bytes.

Application uses bounded 16 MiB chunks, at most 4096, with 64 KiB I/O buffers.
Payloads precede GPT writes; backup GPT regions for all LUNs precede primary
regions. Intent is durable before the first write; every chunk is synchronized
and read back. All final tables and protected ranges are checked before commit.
Six LUNs are coordinated through a journal; they are **not atomic together**.

After interruption, all mirrors and exact coverage are verified again.
Original, target and expected before/after mixtures permit reviewed recovery.
Any unrelated byte, protected-range change, wrong inode or corrupt mirror
blocks writing. Progress counters do not authorize recovery. Recovery needs
the retained journal and original images; it does not require the ROM directory.
Once rollback starts, inspection offers continued rollback rather than reversing
direction. Cancel is allowed only when staging left every target unchanged.

## CLI

Create a private request with schema 1, format ure-stock-job-request, model
(xiaomi-pad-7 or poco-pad-x1), a declared SKU tag, firmware_profile,
stock_inputs_directory, ordered LUN rows 0 through 5, payloads, explicit
erase_android_data and zero_sparse_holes booleans. Each LUN row contains an
absolute image path and an optional absolute same-target identity_backup path.
Payload rows contain lun, label and filename. An empty payload list restores
only stock tables and protects every payload byte.

Commands:

- stock image-inspect IMAGE
- stock job-plan REQUEST --output PLAN
- stock job-execute PLAN --journal NEW_DIRECTORY --confirm PLAN_SHA256
- stock job-inspect JOURNAL
- stock job-resume JOURNAL --confirm PLAN_SHA256
- stock job-rollback JOURNAL --confirm PLAN_SHA256
- stock job-cancel JOURNAL --confirm PLAN_SHA256

CLI results use the existing JSON data envelope. Saved plan files contain the
plan itself. Unknown options, missing decisions and stale choices are refused.

## GUI and evidence

The stock pages select declared model/SKU, the directory containing lun0.img
through lun5.img, verified stock inputs, optional original GPT directories,
explicit A/B slot scope, OS payloads, paired data reset, sparse zero policy and
a journal. Review shows every measured LUN capacity, programming extent,
protected tail, staging budget and warnings. Changing model, SKU, slots,
sources or policies invalidates confirmation. Interrupted journals require
separate inspection before resume, rollback or unchanged-staging cancellation.

Tests use independent logical-byte and OEM XML oracles, pinned raw/sparse
sources, two capacity scales, both model declarations, whole logical LUN
rollback digests, actual child SIGKILL, torn GPT bytes, missing ROM inputs,
unrelated divergence, wrong inodes and actual GUI callbacks. GUI stand-ins
do not prove rendered layout, touch navigation or tablet operation. QEMU user
fixtures validate the extracted ARM64 CLI on host-backed regular files; they
do not prove guest reboot durability or UFS behavior.

See the [build report](../reports/URE-STOCK-JOB-BUILD.md) for exact source, compiler, artifact and
test identities. Actual Pad 7 and POCO Pad X1 capacity/firmware acceptance,
shipping-kernel interruption tests and physical stock boot remain required.
