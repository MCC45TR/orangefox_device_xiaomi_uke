# Comprehensive partition manager and stock-layout restoration

The 1 October scope expansion explicitly requires a comprehensive partition
manager and restoration of the device's default partitions, with bounded memory
and practical large-object I/O. This extends roadmap §8, §17–20, §58 and §70–77;
it does not replace the original 103-topic roadmap. This document records the
implementation and acceptance requirements. It is not a completed manager.

## Current foundation

The [Uke dualboot shell](DUALBOOT-SETUP.md) provides five OS layouts, optional
separate Linux boot, explicit units and a reviewed command/size preview. It
allocates only within original userdata. Host five-role filesystem transactions
and complete rollback are tested. The narrow live F2FS recreation implementation
still has closed profile prerequisites and no physical acceptance; it does not
enable general OrangeFox writes or six-LUN restoration on the tablet.

The shared native library already inspects both GPT copies, CRCs, GUIDs,
partition ranges, overlaps, alignment and protective MBR. Private GPT backups
carry the selected target/profile, raw regions, partition-table JSON and hashes.
Reviewed repair/restore/rollback works on disposable images with durable
journals and actual-byte inspection. Storage identity/ownership observations
and raw local/host-assisted backup/restore provide additional building blocks.
Native stock reconstruction now derives both GPT copies from pinned Global OEM
inputs and selected capacity, preserving original disk/partition identities.
Reviewed per-image stock metadata execution/readback/rollback uses the shared
GPT journal. A separate combined image job now prepares and checks filesystems,
then applies userdata payloads and GPT with one persistent recovery journal.
Six-LUN stock orchestration also works on distinct regular images, with retained
originals and exact selected payload/GPT recovery. Physical writes, encrypted-data
migration, live six-LUN geometry and installed-system boot acceptance remain
unfinished. The layout pages use this
combined job for regular images; they do not authorize a live partition job.

## Explicit capability and admission contract

`partition capabilities` reports source implementations and runtime filesystem
tools for **regular images**, separately from unavailable live-device workflows.
The general `capabilities` result embeds the same contract under
`partition_management`. The layout page exposes a read-only review action.
Tool presence does not validate a selected filesystem, free space, minimum size,
device or firmware; `target_validation_required` remains true.

| Workflow | Current source scope | Additional conditions |
|---|---|---|
| Layout preview | Read-only healthy GPT observation | No filesystem or Android boot acceptance |
| Metadata transaction | Regular image GPT only | Exact plan/target, complete original/replacement records and readback |
| Combined shrink and new OS filesystems | Regular image, original userdata pool only | Unencrypted ext4/F2FS, actual supported tools/checks, confirmed complete journal |
| Before-userdata placement | Regular image erase/recreate | Explicit advanced mode and recreate policy; original files are destroyed |
| Shared ESP | Exact existing payload bytes retained | Set new ESP allocation to zero to retain an existing ESP; migration is unavailable |
| Coordinated six-LUN stock restore | Six regular images, separately pinned Global source | Declared model/SKU tags are not accepted installed unit identities |
| Live repartition, shrink/recreate and six-LUN restore | Unavailable | Exact unit/firmware, six-LUN geometry, FBE/TEE, exclusive ownership, inactive snapshots, fallback and physical durability admission |

Live blockers have stable codes with explanations. They include
`live-storage-writer-unaccepted`, `device-profile-unaccepted`,
`six-lun-geometry-unverified`, `installed-firmware-unverified`,
`android-fbe-trust-unverified`, `virtual-ab-state-unverified`,
`exclusive-storage-ownership-unverified`, `physical-fallback-unverified` and
`forced-restart-durability-unverified`. Source/GUI/CLI cannot remove them by
supplying a profile JSON record, advanced choice or environment variable.

Compound `partition job-* --object` routes refuse with
`live-repartition-unavailable` before selecting a target, opening a request or
journal, using credentials, creating a mapper or invoking mounts. Direct native
job planning checks regular-image scope before GPT or filesystem work. Read-only
inventory and GPT observations remain separate APIs; their observations do not
authorize a live plan. Existing private image recovery journals retain their
captured schema and original byte interpretation.

Encrypted userdata preservation and before-userdata **data migration** remain
unimplemented. Erase/recreate does not solve Android encryption or validate the
new filesystem's Android boot policy. Advanced GUID edits remain explicit GPT
metadata work; content-format changes outside userdata require an unavailable
separate transaction. Advanced mode never bypasses live admission.

Focused host controls include all unavailable live routes with unopened FIFO
requests and missing system roots, direct native refusal without a descriptor,
strict options, actual GUI capability review, shared ESP protection, fscrypt
refusal, host SIGKILL and complete original-image rollback. A front-placement
fixture checks loss of the original file, independent new filesystem signatures,
and exact original-byte rollback. It is a host image result, not evidence that
encrypted Android userdata can be recreated and booted on either commercial unit.

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
before/after sizes, alignment loss, data-loss warnings and a separate review.
The legacy `gpt layout-plan` applies metadata only. The GUI now reviews the
combined `partition job-plan` described below. Editing a selection invalidates
its review.

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

## Combined filesystem and GPT image job

`partition job-plan REQUEST --image IMAGE --sector-size 512|4096 --profile
PROFILE --output PLAN` seals the original userdata bytes, target path/inode,
capacity, both GPT copies, resolved role identities and all protected ranges.
It uses the same original-userdata-only layout arithmetic. No existing OEM
payload, existing Linux/Windows partition or shared ESP is part of its write
pool. Setting the ESP allocation to zero retains an existing ESP and every byte
of its files; registering new OS boot entries is a separate operation.

Preserve mode requires an observed ext4 or F2FS filesystem matching the chosen
userdata type. An unknown, encrypted or fscrypt-enabled source is refused;
F2FS checks both superblocks. A signature without a block-encryption marker
does not prove Android FBE access. Advanced front placement requires explicit
erase/recreate and cannot preserve existing Android data. Recreated userdata
has no established Android boot compatibility. Advanced GUID edits remain
explicit; formatting other existing partition contents requires a separate
range transaction and is refused by this userdata-only job.

Execution requires the exact reviewed digest:

```text
partition job-execute PLAN --image IMAGE --sector-size 4096 --journal NEW_DIRECTORY --confirm PLAN_SHA256
partition job-inspect DIRECTORY --image IMAGE --sector-size 4096
partition job-resume DIRECTORY --image IMAGE --sector-size 4096 --confirm PLAN_SHA256
partition job-rollback DIRECTORY --image IMAGE --sector-size 4096 --confirm PLAN_SHA256
partition job-cancel DIRECTORY --image IMAGE --sector-size 4096 --confirm PLAN_SHA256
```

Select a private, persistent journal outside the disk image. The conservative
free-space budget is three original userdata sizes plus 64 MiB. Sparse zero
staging and reflinks reduce copying where available without reducing that
required budget. Buffers are 64 KiB; application chunks adapt between 1 and
64 MiB and their total count is bounded. This is a complete-backup workflow,
so a large tablet layout requires a correspondingly large external destination.

Before any original write, the job captures the complete original userdata and
both versions of the five GPT regions. It resizes preserved userdata in a
private file, formats new FAT32/ext4/F2FS/Btrfs/NTFS role images only when their
required native tools are available, sets each image to its final partition
capacity and performs an independent read-only filesystem check. Btrfs
formatting is unavailable in the current stock recovery payload. Existing
userdata UUID retention is checked when the probe exposes its UUID.

The application journal seals disjoint byte ranges and before/after hashes.
Payloads are applied before backup GPT and then primary GPT. Each chunk has a
durable intent record, synchronized data and exact readback. Final verification
checks the complete desired range set, the reviewed healthy GPT and all bytes
outside the original userdata/GPT pool. A successful result identifies
`complete_partition_job: true` for this image operation only.

Recovery inspects current bytes rather than trusting saved progress. Original,
desired and interrupted mixtures of the sealed before/after bytes can be
resumed or fully rolled back; unrelated changes disable both actions. Resume
uses prepared journal data and does not need the original formatting tools or
request source. Interrupted staging leaves the original image untouched and
can be cancelled. Cancel never discards the only rollback copy after writes.
GUI recovery offers actions only after inspection and binds the target,
sector size, journal and digest to that review.

Host tests cover actual ext4 file relocation/retention, final FAT32/ext4/NTFS
checks, both sector sizes, existing ESP preservation, advanced front recreate,
SIGKILL interruption, partial writes, changed targets, divergence refusal and
complete original-image rollback. Native ARM64 execution and guest reset are
separate validation stages. Live tablet writes, real UFS reset durability,
Android encrypted-data migration and installed OS boot remain unaccepted.

On 2 October 2026 the owner clarified that **forced reboot** is the primary
tablet interruption scenario. Older power-loss wording remains historical
evidence. A process kill or a generic VM emergency reboot is not an exact-device
forced-reboot acceptance result.

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

## Coordinated stock image job: 3 October 2026

The [six-LUN stock job](STOCK-IMAGE-RESTORE.md) extends the metadata-only workflow
with a shared plan, verified before/after mirrors for every changed range,
selected hash-pinned Global OS payloads, bounded native sparse decoding and
durable inspected recovery. All six image identities and capacities remain
bound. Unselected firmware, calibration and payload tails stay protected;
payloads precede backup GPTs across all LUNs and then primary metadata.
The GUI reviews model/SKU declarations, actual image capacities, A/B destinations,
data-loss/zero policies and staging space. It never switches the active slot.
Neither model declaration is a verified capacity/firmware profile. This regular-
image implementation does not establish physical stock return or live writes.

## Completion evidence

Acceptance needs malformed/overflow/overlap and wrong-unit/profile/SKU tests,
512/4096 synthetic disks, six-LUN stock reconstruction from actual OEM inputs,
capacity variation and open-ended range tests, isolated filesystem/migration
fixtures, actual interrupted operation and recovery tests, extracted AArch64
payload checks, and a rendered operation/review UI. Live acceptance additionally
needs an identified Uke, installed firmware/slot/snapshot/ownership gates,
preserved calibration and stock firmware, and a rehearsed stock-return route.
Current host evidence does not prove these physical results. Per-image stock
GPT reconstruction and coordinated six-LUN OS image restoration are partial
implementations; no full manager, physical stock-return contract or roadmap
phase is marked complete.
