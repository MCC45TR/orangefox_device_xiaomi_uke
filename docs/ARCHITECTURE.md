# Recovery architecture

The target product is the UKE Recovery Environment (URE), an offline
Android/Linux/Windows maintenance layer on OrangeFox. The [comprehensive
roadmap](COMPREHENSIVE-ROADMAP.md) specifies all planned subsystems, APIs, UI
flows and sixteen implementation phases. The workspace [plan](https://github.com/MCC45TR/uke-linux/blob/main/PLAN.md#61-ure-implementation-milestones)
tracks dependencies as URE-00–URE-15; [feature coverage](FEATURE-PARITY.md#ure-capability-extension)
defines the added acceptance contracts.

## Existing baseline

Project-owned C++ currently provides stock XML inventory, Linux/ESP partition
identity and read-only mount plans, rotation-property controls and a separate
native active-slot installer. Host fixtures cover the documented identity and
installer policies. The [public alpha report](../reports/PUBLIC-ALPHA-BUILD.md)
records source, build, package and limited userspace-emulation evidence. Neither
model has physical recovery or stock-return acceptance. A new local [native URE
checkpoint](URE-NATIVE.md) adds the shared library, JSON hierarchy, discovery,
diagnostics and regular-file transaction/editor backend. A general block-storage
transaction engine remains planned.

The next [implementation checkpoint](../reports/URE-STREAMING-BUILD.md) adds
persisted sealed file plans, read-only journal classification, confirmed
resume/cancel, bounded-memory chunked file/image backups and a host receiver.
The GUI and CLI call the same backends. Published chunks and target readback,
rather than a progress record alone, decide what can safely resume.

Raw-image restoration now extends that shared engine with a sealed current-byte
manifest, private original/desired chunk mirrors, full readback and inspected
partial-write continuation or rollback. The CLI and native GUI use the same
policy. All physical block writes remain gated; local restore currently requires
both raw objects in the journal. The separate host-assisted image path binds
complete host backups to a reviewed plan, receives verified original/desired
chunk pairs and keeps only bounded local caches. Host persistence remains an
explicit attestation; live writes and physical acceptance remain unfinished.

The first `uke` BoardConfig and read-only recovery fstab derive from the verified
Global stock package. `scripts/prepare-build-tree.sh` stages the device tree and
applies `patches/0001-preserve-recovery-vendor-directory.patch` to the active
Android build tree, preserving recovery's vendor metadata while copying the
base ramdisk. The read-only control cannot partition or format. The separate
installer checks both slots' stock boot stack, writes only active recovery and
verifies readback; its inactive stock recovery remains protected. Generic
upstream recovery tools/UI do not yet share this policy universally.

## Shared native management layer

The C++ `libuke-recovery` now serves the JSON CLI and native OrangeFox adapter.
Legacy read-only commands remain compatible; the active-slot installer remains
a separate native tool. The complete intended library owns device/firmware identity,
Storage Graph, transactions, filesystem and crypto managers, Android/Linux/
Windows discovery/rescue, boot targets, networking, backups and diagnostics.
`uke-recoveryctl`, the installer and OrangeFox adapters use a versioned structured
interface with bounded parsers and errors. Preserve current CLI compatibility;
the roadmap's commands are proposed syntax until implemented and tested.

Read-only discovery cannot replay journals, write partitions, create mappers,
unlock storage, change slots/EFI variables or unmount as a hidden side effect.
The graph resolves stable disk/LUN/GPT/PARTUUID/range/filesystem/slot identities
and ownership; a caller cannot substitute an arbitrary block path. Unknown
identities fail with a useful explanation. Host automation prefers Bash, tablet
payloads contain no Python, and chroot tools obey the same execution policy.

## Plans, transactions and verification

A future plan contains schema version, operation ID, model/SKU, firmware
profile, LUN identity, sector size, GUIDs/ranges, selected slot, snapshot state,
source artifact hashes, backup manifest, expected changes, verification and
recovery steps. All frontends use discover → identify → diagnose → plan →
back up → revalidate → execute → read back → commit → record recovery path.
Revalidate identities, artifacts, available space, mounts/mappers, snapshots
and power/thermal state immediately before execution. No donor offsets or
source-time side effects are inherited.

Persist transaction intent and completed boundaries durably. Distinguish
`FAILED_SAFE` from `FAILED_UNCERTAIN`; a partial write cannot be reported as
unchanged storage. Inspect interrupted work before safe resume or rollback.
Long operations declare cancellation boundaries, resource ownership and cleanup.
U1 fixture failures precede any controlled H2 device write.

## Rescue, boot and UI contracts

Linux discovery matches installed kernels, initramfs, modules, DT and BLS/EFI
entries without mistaking the recovery kernel for the installed one. The native
editor and file manager preserve Unix metadata and validate bounded atomic
saves. LUKS/BITLK secrets use fd/API input and short-lived buffers; Android FBE
remains separately blocked by installed-firmware KeyMint/TEE trust. Btrfs needs
a compatible recovery kernel; its current stock-profile mount blocker remains.
Windows rescue validates NTFS/WIM metadata and handles ESP/BCD separately.

Boot targets use validated one-shot requests and a proven stock/EFI/Aloha
backend, preserving the default and Android/recovery fallback. No interactive
menu is required by the intended UI; Linux/Windows routing is still planned.
Shared ESP edits preserve other OSes and unknown EFI files. USB exports and
local mounts have exclusive ownership; USB networking precedes opt-in SSH/SFTP
and later Wi-Fi. SSH starts disabled with public-key authentication, a visible
host fingerprint and explicit key persistence.

UI sections cover Systems, Storage, Boot, Rescue, Files, Network, Diagnostics
and Advanced, alongside standard install/backup/restore. Show detected systems,
locked volumes, planned changes, transaction state and missing dependencies.
Persist preferences outside calibration storage. R000–R100 milestones, pstore,
module/display/touch/USB/UFS evidence and redacted reports support diagnosis.
Public and engineering profiles remain distinct. A displayed action is planned
until its own evidence gate passes; source/build results do not create hardware
support records.
