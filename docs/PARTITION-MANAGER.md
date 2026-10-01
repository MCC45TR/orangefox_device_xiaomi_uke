# Comprehensive partition manager and stock-layout restoration

The 1 October scope expansion explicitly requires a comprehensive partition
manager and restoration of the device's default partitions, with bounded memory
and practical large-object I/O. This extends roadmap §8, §17–20, §58 and §70–77;
it does not replace the original 103-topic roadmap. This document records the
implementation and acceptance requirements. It is not a completed manager.

## Current foundation

The shared native library already inspects both GPT copies, CRCs, GUIDs,
partition ranges, overlaps, alignment and protective MBR. Private GPT backups
carry the selected target/profile, raw regions, partition-table JSON and hashes.
Reviewed repair/restore/rollback works on disposable images with durable
journals and actual-byte inspection. Storage identity/ownership observations
and raw local/host-assisted backup/restore provide additional building blocks.
Native stock reconstruction now derives both GPT copies from pinned Global OEM
inputs and selected capacity, preserving original disk/partition identities.
Reviewed per-image stock metadata execution/readback/rollback uses the shared
GPT journal. Physical writes, layout migration, multi-LUN orchestration and
complete filesystem transactions remain unfinished. The layout pages and
original-userdata-only allocation planner described below are now implemented;
they do not authorize a live partition job.

`gpt map` now reports every partition and OEM reserved record, actual byte
ranges, protected label hints and signature observations relative to each
partition. Healthy, agreeing GPT copies are required before free gaps are
calculated. Reservations are never free space. Probing uses at most 8192 bytes
per partition and 128 partitions; additional rows remain visible as unprobed.
Label hints do not establish OS ownership, Android FBE access or write
eligibility. Invalid GPT geometry yields no inferred free-space map. Private
JSON exports retain unit information and must not be published.

## Userdata layout designer

`gpt layout-preview REQUEST --image IMAGE|--object WHOLE_DISK_ID --profile
PROFILE [--output PRIVATE_JSON]` calculates a read-only layout from the current
healthy, agreeing GPT copies. `layout-plan` uses the same arguments and requires
`--output PLAN`. GUI and CLI use the same native arithmetic and policy engine.
The GUI provides keyboard size entry, GB/GiB/MiB/percentage selection,
role-compatible requested filesystems, a proportional colored allocation bar,
before/after sizes, alignment loss, data-loss warnings and a separate review
before applying image metadata. Editing a selection invalidates its review.

All new ESP/Linux/Windows partitions fit inside the **original userdata range**.
Unused GPT gaps and existing OS partitions never extend this pool. Percentages
use the original userdata capacity rounded down to whole MiB. GB is decimal,
GiB and MiB are binary; decimal point or comma is accepted with at most six
decimal places. Allocation rounds down to MiB and discloses discarded bytes.
Only userdata accepts `remaining`, and userdata cannot be deleted. New role
sizes may be zero. Every role must be present exactly once.

Standard mode keeps userdata's original start, GPT slot, type and unique GUID;
it allocates ESP, Linux and Windows in that order after the shortened userdata.
All other existing GPT records retain their raw bytes. An unaligned userdata
start is refused rather than rounded into its data. With all new roles disabled,
`remaining` preserves the original userdata endpoint, including an unaligned
tail. Existing `uke_esp`, `uke_linux` or `uke_windows` records are preserved;
attempting to create a second role with the same label requires a separate
migration workflow.

Advanced mode permits an explicit replacement userdata GUID, an erase/recreate
userdata policy, and explicit GUID or content-format requests for selected
existing partition indices. It does not permit changing other partition ranges
or converting OEM reservation records. Selected GUIDs must be nonzero and
unique; unselected entries remain byte-for-byte unchanged. A requested format
is shown as destructive work still required, not as an operation performed by
the GPT engine. Switching to standard mode clears these advanced requests.

Advanced `before_userdata` places ESP/Linux/Windows at the beginning of the
original userdata extent and moves the new userdata start after them. It
requires `userdata_policy: "recreate"` and explicitly destroys existing Android
userdata. It is not a workaround for encryption and never implies preservation
of encrypted data. Preserve mode requires a separately verified filesystem
shrink; a smaller GPT endpoint alone can leave filesystem data outside its
partition.

Example request, with new roles placed after userdata by default:

```json
{
  "schema": 1,
  "format": "ure-layout-request",
  "mode": "standard",
  "placement": "after_userdata",
  "userdata_policy": "preserve",
  "rows": [
    {"role": "esp", "size": "256", "unit": "MiB", "filesystem": "fat32"},
    {"role": "linux", "size": "20", "unit": "%", "filesystem": "ext4"},
    {"role": "windows", "size": "0", "unit": "GiB", "filesystem": "ntfs"},
    {"role": "userdata", "size": "", "unit": "remaining", "filesystem": "f2fs"}
  ],
  "record_edits": []
}
```

For a front-placement preview set `mode` to `advanced`, `placement` to
`before_userdata`, and `userdata_policy` to `recreate`. For an explicit advanced
record request use, for example, `{"index": 4, "partuuid":
"11111111-2222-4333-8444-555555555555", "contents": "preserve"}`. This is syntax
only: select an index from the actual map, never assume index 4 belongs to a
particular device partition. A content-format request uses `contents: "format"`
and an explicit `filesystem`; its payload work remains unavailable.

The resolved request includes generated GUIDs, canonical physical row order
and explicit policies. Repeating it on the same unchanged target produces the
same layout digest. Plans bind target identity, current GPT, full request and
five original/proposed metadata ranges. Image `gpt execute` requires the exact
plan digest, backs up both versions, writes backup GPT before primary GPT and
verifies readback; inspected rollback restores the original metadata exactly.
The result is marked `GPT_METADATA_ONLY`, `formats_filesystems: false`,
`migrates_data: false` and `complete_partition_job: false`. Advanced mode does
not bypass the common live-write gate.

Live completion still requires device/model/SKU and installed firmware
verification, original multi-LUN and off-device data backups, exclusive UFS
ownership, installed Android FBE trust, snapshot/merge and super checks, supported
filesystem shrink or Android-compatible recreate, new filesystem formatting,
readback and a preserved stock recovery route. Pad 7 and POCO Pad X1 require
separate original-device evidence; a shared marketing or codename assumption
cannot authorize another unit. See [URE-PARTITION-LAYOUT-BUILD.md](../reports/URE-PARTITION-LAYOUT-BUILD.md)
for software/build evidence.

## Required Linux backup workflows

The additional 1 October requirement includes three distinct backup types:

* Whole Linux partition content, with source identity, geometry, filesystem
  observations, verified chunks and restart/recovery behavior. The existing raw
  storage backup is a foundation; installed-system selection and review remain
  required.
* A selected Linux home tree, preserving numeric ownership, permissions, POSIX
  ACLs and accessible extended attributes, symlinks, hardlinks, sparse files and
  timestamps. Traversal must stay beneath the selected root, disclose mount
  boundaries and unsupported special files, refuse inaccessible metadata and
  detect changes without claiming an atomic snapshot of a running home tree.
* Btrfs subvolume backups as verified full or incremental send streams from
  read-only snapshots. Incremental backups bind the exact parent identity and
  require the corresponding unchanged receiver parent. Snapshot-only local
  copies do not replace external backups. Kernel support, stream validation,
  destination isolation, receive/restore and physical acceptance are separate
  requirements.

Backups are private data. Output must remain outside the selected source tree,
use private durable records and bounded streaming buffers, and distinguish an
incomplete capture from a verified complete backup. Current stock recovery
kernel Btrfs support remains unavailable; userspace implementation does not
change that fact.

## Device-derived stock inputs

The measured Global `OS3.0.303.0.WOZMIXM` archive has SHA-256
`f811ae6255b7535d32f80548d800487a6494a87ddb4cca592799337fab24cd0d`.
`manifests/stock-layout-global.json` records 138 rawprogram entries over UFS LUNs
0–5, with 4096-byte sectors. These entries include GPT programming regions and
open-ended ranges; they are not 138 interchangeable user partitions.
The matched CN profile is separate even where its rawprogram map agrees.

The archive includes `gpt_main0..5.bin`, `gpt_backup0..5.bin`, `gpt_both0..5.bin`
and `patch0..5.xml`. Stock restoration must derive metadata from these verified
inputs or a verified device-original backup. It must not copy Nabu tables,
assume a 512 GB SKU from a filename, execute OEM/donor flash scripts, or replace
unit-bound manifests with generic offsets.

`scripts/describe-stock-gpt.sh` rechecks the complete OEM archive and all thirty
GPT/rawprogram/patch inputs against the original extraction record, validates
XML and native range inventory, and compares it with the public stock map.
`manifests/stock-gpt-global.json` records exact source-file hashes without private
paths. This catalog is input provenance, not reconstructed device metadata or
permission to restore it. The separate native reconstruction engine checks the
five exact input hashes for the selected LUN; it never executes OEM scripts.

LUN0's rawprogram leaves `userdata` open-ended at sector 2961408. Its patch
records update the last partition endpoint, last usable LBA, alternate/current
header LBAs, backup-table location, table CRCs and header CRCs from the actual
`NUM_DISK_SECTORS`. Other LUNs also contain capacity-dependent terminal metadata.
A template's fixed header values are not proof of a device's capacity. Native
reconstruction applies the exact reviewed capacity and CRC semantics of those
pinned patches, and validates both resulting copies against selected capacity.
It is not a general XML expression interpreter.

## Implemented stock metadata workflow

`gpt stock-preview INPUTS --capacity-bytes BYTES --lun 0..5 --profile
global-os3.0.303.0 --output DIRECTORY` creates a private five-region metadata
preview. Its OEM template GUIDs are explicitly unbound, and the preview is not
an original-unit backup or an executable restore plan.

`gpt stock-plan INPUTS --image IMAGE --lun 0..5 --profile
global-os3.0.303.0 [--identity-backup ORIGINAL_GPT] --output PLAN` binds the
reconstructed metadata to the selected image. Stock GUIDs must come from its
healthy current GPT or a verified same-target, capacity/sector/profile-matched
original GPT backup. Missing identities are refused. Unknown extra partitions
remain protected even when an older backup supplies stock identities. Known
`uke_linux`, `uke_windows` and `uke_esp` removals are explicitly listed with
before/after ranges and effects on OS visibility. Declared firmware scope is
not installed-device firmware verification.

The selected LUN's five OEM files must match compiled source pins. Revalidation
before execution regenerates the desired metadata and matches all reviewed
hashes and tables. `gpt execute`, `journal-inspect`, `resume` and `rollback` share
the existing durable metadata engine. Each image transaction retains original
and desired ranges with ordered backup-first writes and readback. Rollback does
not need the OEM directory after those journal copies are verified. Physical
writes remain closed by the common storage gate. This workflow restores table
metadata only, with no filesystem resize, data migration or Android payload
restoration claim.

The OEM templates for LUNs 1–5 contain a `last_parti` record with zero type GUID,
nonzero unique GUID, vendor attribute bit 60 and a terminal reserved range.
The native inspector reports this explicitly in `reserved_records`, excludes
it from `partitions`, includes it in overlap validation and preserves its
identity. Other nonzero unused entries remain invalid. The
[UEFI GPT type table](https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html#defined-gpt-partition-entry-partition-type-guids)
defines a zero type GUID as unused; the narrower reservation recognition here
is derived from the pinned Uke OEM inputs. The stock protective MBR count is
also corrected for selected capacity; OEM templates use a saturated count even
for smaller LUNs. No firmware image contents are modified by reconstruction.

Native tests compare all six LUNs at two capacities against an independent
host-only XML patch oracle, then test original GUID preservation, reviewed
metadata execution/readback/rollback, simulated partial metadata recovery,
missing-identity backup recovery and refusal cases. The oracle uses host
libxml2; libuke and the tablet CLI do not acquire that dependency. UI stock
input/identity/review pages are present; rendering and touch acceptance remain
separate. Multi-LUN atomicity is not claimed.

## Required manager behavior

| Area | Required behavior and evidence |
|---|---|
| Discovery | Whole-unit/LUN capacity and sector geometry, primary/backup GPT health, type and unique GUIDs, labels, ranges, gaps, alignment, bounded filesystem/encryption probes and explicit unknown OS ownership; source and host fixtures precede physical acceptance |
| Planning | Create/delete/rename/type/attribute/resize/move previews, capacity-derived stock/Android/Fedora/Windows/custom layouts, all affected installations and exact before/after ranges; no guessed free space or filesystem shrink minimum |
| Protection | Preserve stock early firmware, calibration/security ranges, unknown partitions and other ESP files; distinguish metadata-only changes, data migration, filesystem work and boot-policy changes |
| Data operations | Format/check/resize/copy/restore only through reviewed filesystem adapters and mature transaction/backup gates; verify hashes and metadata, expose meaningful cancellation, interruption and rollback semantics |
| Stock restoration | Explicit model/SKU/profile/unit/LUN/sector/capacity match; verified source files and original multi-LUN/boot-chain backup; show every removed/resized partition and affected OS; restore the selected stock layout without claiming user data or calibration was recreated |
| Multi-LUN journal | Durable per-LUN intent, original/desired records, ordered writes/readback, uncertainty and recovery routes; never claim atomic all-LUN commit across interruption |
| UI | A complete partition map, capacity/ownership state, operation queue, warnings tied to affected data, exact plan review and current-byte recovery actions; require rendering/touch acceptance separately |
| Optimization | Bound metadata/read buffers and caches, avoid full-object scans per chunk, copy unchanged ranges efficiently, retain complete boundary verification and truthful progress; no fabricated throughput claims |

Stock partition metadata restoration does not itself restore Android partition
contents. A separate firmware-matched image/boot-chain plan must identify the
payload for every affected partition and preserve the known-good recovery route.
Formatting userdata destroys its contents; it does not unlock wrapped keys or
prove installed KeyMint/TEE trust. Multi-OS layout selection cannot implicitly
change default boot policy.

## Completion evidence

Acceptance needs malformed/overflow/overlap and wrong-unit/profile/SKU tests,
512/4096 synthetic disks, six-LUN stock reconstruction from actual OEM inputs,
capacity variation and open-ended range tests, isolated filesystem/migration
fixtures, actual interrupted operation and recovery tests, extracted AArch64
payload checks, and a rendered operation/review UI. Live acceptance additionally
needs an identified Uke, installed firmware/slot/snapshot/ownership gates,
preserved calibration and stock firmware, and a rehearsed stock-return route.
Current host evidence does not prove these physical results. Per-image stock
GPT reconstruction/metadata restoration is a partial implementation; no full
manager, stock-return contract or roadmap phase is marked complete.
