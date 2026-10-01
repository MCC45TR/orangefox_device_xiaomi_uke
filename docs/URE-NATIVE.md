# Native URE implementation

This is an implementation checkpoint, not completion of the comprehensive
roadmap. The project now owns a common C++ library, a JSON CLI, host fixtures
and an OrangeFox adapter. The library is statically linked into the CLI and
recovery GUI. The final capability contracts remain in
[FEATURE-PARITY.md](FEATURE-PARITY.md). Device boot, touch, display, storage
writes and rollback have no physical acceptance record.

## Implemented interfaces

`uke-recoveryctl help` lists the actual interfaces. The older `list`,
`plan-mount`, `mount-ro`, `unmount` and `rotation` interfaces remain compatible.
New commands emit schema-1 JSON with an operation ID, timestamps, warnings and
a result. Errors return a nonzero exit status. A failed subprocess exposes its
status and returns failure rather than silently reporting success.

| Interface | Current behavior |
|---|---|
| `capabilities` | Lists all 24 contracts, implementation scope, required tools and observable kernel support; never creates hardware evidence |
| `storage inventory/graph/mounts/health` | Reads sysfs device numbers, labels, PARTUUIDs, 512-byte capacity units, parent LUNs, mapper dependencies and mount associations; these aliases currently return the same graph, not a UFS wear analysis |
| `storage inspect STABLE_ID` | Read-only live selection by full sysfs/PARTUUID/GPT identity, device-number and capacity checks, sector ioctls, parent-GPT partition correspondence and private unit/LUN evidence; positive live-device acceptance is pending |
| `storage usage STABLE_ID` | Bounded mount-namespace, writable-FD, swap, configfs backing and mapper/parent ownership observations; missing evidence, unknown dependencies and cycles refuse a quiescent classification |
| `gpt inspect --image FILE --sector-size 512/4096` or `--object STABLE_ID` | Both headers/tables, CRCs, reciprocal positions, reserved metadata, GUID/range overlap, UTF-16 labels and protective MBR inspection |
| `gpt backup/verify/compare` | Private five-region raw metadata backup, partition-map JSON, identity/profile manifest, checksums and sparse reconstruction of the actual saved GPT; image or read-only whole-LUN source |
| `gpt repair-plan/restore-plan/execute/rollback` | Sealed current/desired table review, same-target backup checks, durable original/target metadata journal and ordered readback writes for regular images; live UFS writes remain gated |
| `gpt journal-inspect/resume` | Verified backup/payload reads, original/target/partial/divergent classification and explicit readback-only commit; interrupted metadata writes are never replayed |
| `filesystem inspect --image FILE` | Bounded signature detection for ext4, FAT, F2FS, exFAT, NTFS, XFS, EROFS, Btrfs, LUKS, BITLK and WIM; a signature is not full filesystem validation |
| `filesystem check --image FILE` | Reviewed read-only/no-action checker arguments, inherited read-only descriptor, bounded output and timeout; availability depends on the packaged checker |
| `linux detect/info/kernels/boot-entries/diagnose --root ROOT [--esp ESP]` | Bounded os-release and fallback discovery, package database indicators, kernel/initramfs/module consistency, BLS references, UKI filenames and ELF architecture; does not execute installed programs |
| `windows detect/info/boot/diagnose --root ROOT [--esp ESP]` | System32, registry-hive and recovery-directory indicators; optional Microsoft EFI/BCD presence; no guessed edition/build or registry/BCD editing |
| `files list/search --root ROOT` | Bounded metadata and filename search; prevents relative traversal and following filesystem symlinks |
| `editor read/validate/plan --root ROOT` | Bounded text reads, fstab/crypttab/BLS/JSON validation and sealed file-replacement plans |
| `transaction validate/execute/show/rollback` | Exact plan confirmation, persisted sealed plan, root/file fingerprints, backup/readback, durable journal boundaries, atomic replacement and verified rollback |
| `transaction inspect/list/resume/cancel --root ROOT` | Read-only journal discovery and current-state classification; explicit confirmed recovery or cancellation only when the recorded phase, identities and verified backup permit it |
| `backup file --root ROOT` | Private, exclusive-create backup and manifest for a bounded regular file; not a partition/image backup service |
| `backup plan/storage-plan/capture/resume/verify/export` | Bounded-memory regular-file or identity-bound storage streams, chunk/full SHA-256, durable exclusive publication, verified resume and binary host export; live software requires complete usage/unit/boot evidence and a retained read-only kernel claim; physical acceptance and restores remain pending |
| `boot targets/plan` | Detects candidate components and serializes a one-shot request; execution remains blocked without an accepted Uke boot backend |
| `diagnose SCOPE` | Private bounded kernel, module, pstore, display, input, USB, network, power, thermal and property observations; preserves pstore and does not assert a root cause |
| `report --output FILE` | Public allowlist summary; omits raw pstore, command lines, module addresses, UUIDs, mounts, stage text and raw class fields |
| `android info/slots/super` | Observations and read-only boot-control/lpdump calls; missing HALs/tools cause explicit failure; no slot switch, super write or FBE unlock |
| `wim info/verify --image FILE`, `ntfs info --image FILE` | Fixed reviewed tool arguments and read-only source descriptors; WIM/ESD application and NTFS resizing are not managed transactions yet |
| `crypto detect/info --image FILE` | Signature discovery and conditional cryptsetup header inspection; cryptsetup is archived but not packaged, so inspection requiring it fails explicitly |
| `btrfs subvolumes/usage/scrub-status/balance-status/device-stats --root ROOT` | Conditional read-only wrappers; requires a mounted Btrfs root and a packaged tool. The stock recovery kernel has Btrfs disabled and this candidate does not package btrfs-progs |

The text buffer accepts valid UTF-8 up to 1 MiB and rejects NUL bytes, overlong
UTF-8, surrogates and invalid scalar values. Serialized JSON is bounded to 4 MiB
to accommodate escaping. The GUI is a line editor with previous/next, insertion,
deletion, undo/redo, find/replace, preview and a review/confirm save page. GUI lines
are limited to 8192 bytes. Undo history has both an entry and byte budget.

## File transaction boundary

Only existing regular files with one hard link are writable through this engine.
The plan seals the payload, firmware-profile name, root device/inode, target
device/inode, size, content hash, UID/GID/mode, timestamps and attribute digest.
ACL, SELinux labels and file capabilities use the extended-attribute path.
All path components open relative to retained directory descriptors with
`O_NOFOLLOW`; a symlink cannot redirect an edit outside the selected root.

Execution revalidates, verifies the backup, records READY/EXECUTING boundaries,
writes a temporary file, preserves ownership/mode/attributes, fsyncs, replaces
the file and verifies content/metadata. Failure after execution begins is
`FAILED_UNCERTAIN`; it is never described as unchanged. Rollback refuses unrelated
content or changed metadata. Advisory locks do not stop an unrelated writer;
revalidation detects observed changes, but this is not a filesystem-wide lock.
Hostile concurrent-writer and physical power-loss acceptance are still open.

New journals contain the immutable `plan.json` before any target write. Records
and backups are private regular files inside an owned mode-0700 directory;
directory creation and record operations retain directory descriptors. Journal
mutations share a nonblocking lock. Inspection reads the actual target and backup
and classifies unchanged original identity, verified payload, original restored
content or unrelated divergence. It never replays a transaction automatically.
Resume can apply a verified READY plan to the exact unchanged original, or finish
the commit record after an EXECUTING/VERIFYING interruption when the payload and
metadata already match. The latter does not rewrite the target. Incomplete or
corrupt backups, changed identities and unrelated contents refuse recovery.
Cancellation records `CANCELLED_SAFE` only while the original identity remains
unchanged. Legacy journals without a persisted plan retain verified rollback,
but cannot use the new resume interface.

Plans, backups and journals are private records. The GUI permits selecting a
journal parent and reviewing its destination before saving. Its default `/tmp`
is volatile across reboot; select a verified persistent destination before
relying on it as a recovery record. The CLI accepts an explicit journal
directory. This file engine does not authorize
block-device writes, GPT edits, mapper creation, slot changes or firmware writes.
The separate active-slot recovery installer keeps its existing policy.

## GPT and live selection boundary

Live selection opens only a uniquely resolved block node through the selected
system root and compares its device number, size and logical sector ioctl with
sysfs. Partition selection additionally requires matching valid parent GPT
copies and the same index, PARTUUID, label, start and length. Private records
retain unit-identity evidence when available, a LUN address and boot identity.
Unavailable unit evidence is explicit. A device path or matching GUID alone
cannot authorize a restore. This is source/fixture evidence; real UFS identity,
cross-boot stability and exclusive ownership still need acceptance.

The parser requires both GPT copies to reserve at least 16 KiB for their entry
arrays and separates headers/tables from usable LBAs, following the
[UEFI GPT format](https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html).
It validates each copy independently and decodes UTF-16 partition names.
Alignment is an observation, not a claim that an installed layout may be moved.

`gpt backup` exclusively creates an owned mode-0700 directory containing five
mode-0600 raw ranges, a partition map, a manifest and SHA256SUMS. Verification
reconstructs a sparse in-memory disk from the stored bytes and checks the real
GPT and region geometry against the manifest; self-consistent hashes cannot
substitute arbitrary offsets. Restore requires a healthy backup, the same
image inode/path or verified unit/LUN evidence, capacity, sector size and declared
profile. The declared profile is not installed-firmware verification.

Repair accepts exactly one valid copy and a protective-only MBR. Two valid but
conflicting copies require separate investigation. Plans contain the original
and proposed tables and seal identities, metadata ranges, hashes and source
backup. Restore refuses metadata destinations inside currently usable space;
a layout/migration transaction is a separate feature. Image execution checks for
a live loop attachment, revalidates, backs up every changed range and stores
both original and target bytes before EXECUTING. Backup GPT is published before
primary GPT, with fsync and readback at each range. The healthy result must equal
the reviewed table before COMMITTED.

Journal inspection checks both saved payloads and protected unchanged metadata.
Rollback accepts only bytes from the sealed original/target metadata, refusing
unrelated divergence, and verifies the restored original ranges. Readback resume
only closes a recorded EXECUTING/VERIFYING/FAILED_UNCERTAIN phase when all target
bytes and the reviewed table already match; it does not replay writes. Incomplete
backups cannot use this recovery path. Process fixtures do not establish
electrical power-loss behavior. Advisory locks do not exclude unrelated writers.

The GUI provides target selection, private backup destination, inspection,
comparison, plan review and journal recovery. Its image sector setting accepts
512 or 4096; live geometry is detected. A live target has no execute button, and
the backend independently refuses all real block writes pending common
firmware/slot/snapshot and storage-ownership integration. This checkpoint does
not resize partitions, format filesystems, reread a live kernel partition map or
write stock boot firmware.

Example on an explicitly selected regular image (the journal/backup parent
directories must already exist). Read the complete plan and supply its displayed
`plan_sha256`; the placeholder below is not a valid confirmation:

```sh
uke-recoveryctl gpt inspect --image /mnt/linux/disk.img --sector-size 4096
uke-recoveryctl gpt backup --image /mnt/linux/disk.img --sector-size 4096 \
  --profile global-os3.0.303.0 --output /mnt/backup/gpt-original
uke-recoveryctl gpt verify /mnt/backup/gpt-original
# Repair planning requires exactly one valid GPT copy:
uke-recoveryctl gpt repair-plan --image /mnt/linux/disk.img --sector-size 4096 \
  --profile global-os3.0.303.0 --output /tmp/gpt-plan.json
uke-recoveryctl gpt execute /tmp/gpt-plan.json --image /mnt/linux/disk.img \
  --sector-size 4096 --journal /mnt/backup/gpt-journal --confirm REVIEWED_PLAN_SHA256
uke-recoveryctl gpt journal-inspect /mnt/backup/gpt-journal \
  --image /mnt/linux/disk.img --sector-size 4096
```

## Streaming backup boundary

`backup plan` reads a sized regular file without changing it, records the selected
root and file identities, timestamps, declared firmware profile, source size,
chunk geometry, SHA-256 values, creation time, native format version, compression
and restore requirements. The profile is declared, not hardware validated.
Sources may be file-backed filesystem images, boot images or ordinary files.
Regular-file plans retain schema 1. `backup storage-plan` uses schema 2 and an
explicit image path or stable live Storage Graph identity, with sector size,
capacity, GPT/partition geometry where present and private unit/LUN/boot evidence.
The engine never reads Android credentials, unlocks a mapper or mounts a source.
The declared firmware profile remains unverified and does not authorize restore.

The data buffer is 64 KiB. Chunk sizes range from 64 KiB to 64 MiB, default to
16 MiB and are limited to 16384 chunks and a 4 MiB manifest. Sparse holes are
transferred as their logical zero bytes; this is not sparse encoding or
compression. File planning scans once; storage planning scans twice and compares
every chunk and the full hash. Capture rechecks source identity,
each planned chunk hash and a readback hash before fsync and exclusive
publication. The full backup is verified before COMPLETE. Resume derives its
next boundary from verified files, rather than trusting an older progress
record. Corruption and gaps refuse continuation. Incomplete temporary files
remain discoverable after a process interruption.

For live sources, planning, capture and each export acquire a retained read-only
`O_EXCL` block claim. The selector records successful acquisition: the inspected
stock kernel clears that flag before `F_GETFL` could recover it. Claims are not
a global exclusion of uncooperative raw writers. Usage checks include the parent
disk and dependent mappers, visible process mount namespaces and writable
descriptors, active swap, and configfs mass-storage backing. A partition does
not inherit independent sibling mounts; selecting the whole LUN includes its
children. Unknown holders, dependency cycles/depth, missing proc/configfs,
restricted process visibility and unhandled legacy USB storage block capture.
FD metadata is checked before and after parsing its flags/inode to reject reuse.
Proc mount parsing follows the [kernel proc documentation](https://www.kernel.org/doc/html/v6.8/filesystems/proc.html).

Live destinations on the selected device or related mapper are rejected. Source
identity and usage are rechecked before/after each chunk. Storage capture and
the final exported chunk additionally reread the complete current source before
success. This binds observed bytes to the plan, not to an atomic filesystem or
application snapshot. It does not provide a global writer lock or electrical
power-loss guarantee. Positive physical block-claim/usage acceptance is untested.

`backup export` writes only a selected chunk to stdout. JSON errors go to stderr,
including errors after a partial transfer; receivers must check process status,
byte count and the planned chunk digest before accepting it. Offline verification
proves the stored data against the sealed manifest, not current source stability
or application consistency. A manifest is not permission to restore a device.

The host-only Bash receiver in `scripts/receive-backup.sh` supports a local CLI,
ADB exec-out or an already configured SSH endpoint. It requires an existing
source plan and a host copy of that plan, checks existing chunks on reconnect,
uses strict SSH host-key checking and quotes every remote argument for a POSIX
shell. It does not start a listener, import keys, unlock volumes or configure USB.
The tablet stores only the plan when export is used; backup data can reside on
the host. Local and mocked SSH transport tests are separate from real USB/SSH
or tablet evidence.

Storage plans use their sealed source identity and optional `--system-root`
context; file plans require `--root`. Mixing the contexts is rejected. The host
receiver infers the context from the verified manifest, with `--source-root` for
files or optional `--source-system-root` for storage. The GUI separately selects
an image or live identity, shows usage and plan review, and checks source/sector
and destination selections before capture. It uses 64 MiB chunks for up to
1 TiB within the existing manifest limit. GUI rendering remains unverified.

Example using an explicitly selected, already mounted root:

```sh
uke-recoveryctl backup plan images/root.img --root /mnt/linux \
  --profile global-os3.0.303.0 --output /tmp/backup-plan.json
uke-recoveryctl backup capture /tmp/backup-plan.json --root /mnt/linux \
  --journal /mnt/backup/root-image
uke-recoveryctl backup verify /mnt/backup/root-image
uke-recoveryctl backup resume /mnt/backup/root-image --root /mnt/linux
# On the host, after explicitly copying the private plan and configuring SSH:
bash scripts/receive-backup.sh --manifest backup-plan.json --output backup-store \
  --source-root /mnt/linux --source-plan /tmp/backup-plan.json \
  --source-cli /system/bin/uke-recoveryctl --ssh recovery-host
```

Example for an already accessible image; live selection substitutes `--object`
with the full stable identity and requires all current usage checks:

```sh
uke-recoveryctl backup storage-plan --image /mnt/linux/disk.img \
  --sector-size 4096 --chunk-size 67108864 --profile global-os3.0.303.0 \
  --output /tmp/storage-plan.json
uke-recoveryctl backup capture /tmp/storage-plan.json --journal /mnt/backup/storage
uke-recoveryctl backup resume /mnt/backup/storage
bash scripts/receive-backup.sh --manifest storage-plan.json --output host-store \
  --source-plan /tmp/storage-plan.json --source-cli /system/bin/uke-recoveryctl --adb
```

Resume on another recovery boot refuses if the root/file or sealed live boot
identity changed. Stable cross-boot plans and managed restores still require
the storage transaction backend. SIGKILL tests prove process recovery at a durable
chunk boundary, not storage-controller behavior during electrical power loss.

## Build tools and source boundaries

The candidate adds source-built NTFS mounting/formatting/checking and ntfsresize,
exFAT checking/dumping/formatting, F2FS checking, WIM/ESD tooling and Dropbear.
Raw upstream binaries expose their own interfaces; only the operations above
are integrated into the URE management policy.

wimlib 1.14.5 is pinned to `cd5e231c348c255ae5088873b5a66ee0eb96fa07`.
It builds with NTFS support and without FUSE, using the project-owned Bionic
configuration. The combined NTFS build uses the GPLv3-or-later option documented
by upstream. Dropbear 2025.89 is pinned to
`179de98f7b9584a309ffc48e39c61da940760740`; password/PAM authentication,
forwarding, SFTP and re-exec are disabled. It is a packaged binary, not a managed
remote-rescue service: no listener or persistent host key is created at startup.
There is no SFTP server in this candidate.

cryptsetup 2.8.8 (`f1dbcce190719c9e177e8fbaf3df0b908f87d920`) is a verified
full-history reference with an offline-restorable bundle. Its Android dependency
closure, secure unlock lifecycle and malformed-volume tests are unfinished.
It is not copied into the recovery payload. JSON uses the locked Android JsonCpp
source (`8f65e4b14b9946910e1519231097870b5953b00f`) on host and target.

Reference code is not executed. `prepare-build-tree.sh` extracts pinned source
snapshots into the active build tree and applies the project adapters and five
reviewed patches. The Android repo/boot tools remain documented host-only
upstream build dependencies; no project Python or tablet interpreter is added.

## Remaining roadmap work

| Contracts | Remaining implementation / acceptance |
|---|---|
| C01–C03 | Full live identity/ownership acceptance, general storage transactions, physical power-loss tests and actual R000–R100 producers/correlation; bounded ownership policy and explicit file journal recovery have fixtures, while diagnostics only reads a stage file if one exists |
| C04–C07 | Multi-root orchestration, installed DT/UKI contents and filesystem-root consistency, full metadata browsing/snapshot comparison, config semantic validation, GUI rendering/touch acceptance |
| C08–C09 | Controlled chroot with audited native executable closure; profile-compatible recovery kernel/modules and disposable-media/hardware acceptance. Fedora package/initramfs repair must not execute a target Python dependency |
| C10–C13 | Packaged cryptsetup and secure LUKS/BITLK lifecycle, header/key workflows, Btrfs kernel/userspace, snapshots, rollback, scrub/balance and send/receive transactions |
| C14–C15 | Multi-LUN GPT and boot-chain orchestration, live restore/repair, layout changes, formatting/resizing transactions, OTA/super/snapshot management, second-Android isolation and installed-firmware KeyMint/TEE trust; GPT image backup/repair/restore and read-only live selection are implemented |
| C16–C17 | Accepted Uke Aloha/stock boot backend, request consumption, boot history, retry/rollback policy and default preservation |
| C18–C21 | Managed key-only SSH/SFTP and exclusive USB ownership, live/cross-boot stream acceptance and restores, sparse/compressed formats, Wi-Fi, WIM/NTFS/BCD restore transactions and multi-OS partition designer; identity-bound storage stream software and a host receiver are implemented |
| C22–C24 | Signed update/profile lifecycle, persistent UI/session policy, full reproducibility/CI, optional web/NAS/extensions/forensics/hardware-test tools |

The supplied 103 topics remain design targets. No contract or full phase is
marked complete merely because a native library, a UI page or an upstream tool
now compiles. See [the candidate build report](../reports/URE-NATIVE-BUILD.md)
for exact evidence and hashes.

## Host verification

From the recovery component:

```sh
cmake -S src/device/xiaomi/uke/recoveryctl -B build/ure-host -G Ninja
cmake --build build/ure-host -j4
ctest --test-dir build/ure-host --output-on-failure
bash tests/check-ure.sh
bash tests/check-backup.sh
bash tests/check-storage-backup.sh
bash tests/check-gpt.sh
UKE_RECOVERYCTL_BINARY=build/ure-host/uke-recoveryctl bash tests/check-recoveryctl.sh
bash tests/check-installer.sh
```

Tests cover valid and corrupt 512/4096-byte GPT images, hybrid/protective MBR
geometry, metadata overlap, zero and case-insensitive duplicate identities,
filesystem UUID byte order, duplicate storage identities, sysfs escape attempts,
invalid JSON and UTF-8, stale content/metadata, confirmation, backup/readback,
atomic edit, attribute preservation, rollback, persisted-phase recovery,
readback-only commit, cancellation, journal locks, streaming chunk/full hashes,
wrong-root and corrupt-backup refusal, binary framing, quoted mock transport,
actual SIGKILL/verified resume and public-export omission of
raw crash records and unit identifiers. GPT fixtures additionally cover private
metadata backup and actual-byte reconstruction, same-inode/profile binding,
both-copy repair, ordered restore, usable-area protection, partial rollback,
divergence, locked journals and readback-only resume. These are host fixtures,
not tablet tests.

`bash tests/run-native.sh` records these host gates against their exact source
inputs. `scripts/audit-recovery-image.sh IMAGE REPORT_JSON --qemu` extracts the
actual compressed ramdisk in a restricted host namespace, scans every ELF and
embedded ZIP, checks the GUI XML/tool manifest, and runs AArch64 fixtures. QEMU
covers the CLI file/GPT transaction/rollback and inspected readback recovery,
GPT backup/repair/restore, large-file backup and host receiver,
WIM capture/verify/apply round trip,
ext4/exFAT/NTFS no-action checks with unchanged image hashes, and ephemeral SSH
host-key generation. It never starts an SSH listener or exercises a tablet.
