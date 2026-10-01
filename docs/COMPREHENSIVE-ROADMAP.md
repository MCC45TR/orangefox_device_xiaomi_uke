# UKE Recovery — Comprehensive Architecture and Development Roadmap

> **Target devices:** POCO Pad X1 / Xiaomi Pad 7 (`uke`, SM7675)
>
> **Recovery base:** OrangeFox, Android 16 source line where appropriate, with project-owned Uke management layers
>
> **Project direction:** Android + Linux + Windows + UEFI/Aloha rescue, storage, boot, diagnostics and installation environment
>
> **Document date:** 2026-09-30
>
> **Status:** Architecture / implementation roadmap. A feature is not considered working until its required test gate passes.

## Integration and evidence status

This roadmap incorporates the supplied `UKE_Recovery_Comprehensive_Roadmap.md`
dated 30 September 2026, retaining all numbered topics 0–102. The input SHA-256
is `d9f5787316736eca07b93c47931c9d86ffd2d2a3d068b6c7bba137a34466a4ca`.
The workspace [development plan](https://github.com/MCC45TR/uke-linux/blob/codex/ure-roadmap-integration/PLAN.md#61-ure-implementation-milestones)
tracks its sixteen phases as URE-00–URE-15, alongside the existing 100 platform
steps. [Feature coverage](FEATURE-PARITY.md#ure-capability-extension) provides
stable acceptance IDs for the additional capabilities.

This is planned product scope. Source trees, APIs, command hierarchies, JSON,
screens, storage diagrams and workflows below describe proposed designs unless
explicitly tied to existing evidence. Current command syntax is documented in
[the native checkpoint](URE-NATIVE.md) and [Linux/ESP tools](LINUX-ESP-TOOLS.md);
section 6 includes command families that remain unfinished. Phase order is a
dependency guide: independent host work can
continue while physical gates wait for a device.

| Existing evidence | Current limit |
|---|---|
| The native shared library/JSON CLI, Storage Graph, Linux/Windows discovery, file editor/journals, chunked image backups and GPT image backup/repair/restore have host fixtures. | General live storage ownership/write policy, controlled chroot, OS repair and full GUI acceptance remain unfinished. Fixture checks do not demonstrate a live mount or UFS write. |
| The Global stock303 recovery and native active-slot installer are built; package, privacy and payload checks are recorded in the [public alpha report](../reports/PUBLIC-ALPHA-BUILD.md). | Neither commercial model has boot, display, touch, USB, installed-slot or stock-return acceptance. |
| Installer policy has host negative tests and a QEMU refusal without boot-control evidence; native file/image transactions add verified backups, durable journals and inspected recovery. | Device write/power-loss acceptance and shared preflight for all upstream controls remain open. |
| Stock kernel, module and boot-profile inputs are recorded separately. | Btrfs mounting is blocked by the selected stock kernel; LUKS/BITLK and networking remain planned. Android FBE is blocked pending installed-firmware KeyMint/TEE trust evidence. |

The [hardware ledger](https://github.com/MCC45TR/uke-linux/blob/main/DEVICE-STATUS.md)
remains the authority for physical results; this roadmap creates no device test
records. Public support requires the exact model/SKU/firmware gates, not only a
source or package result. Windows and additional OS layouts are conditional
future scope; Fedora Rawhide AArch64 remains the first Linux target. P3 features
remain optional as identified in sections 92 and 95.

---

## 0. Executive Summary

UKE Recovery should not be treated as a normal OrangeFox device port with a few extra scripts. The long-term target should be a **device-specific offline system maintenance environment** that can discover, diagnose, unlock, repair, back up, restore, repartition and boot Android, Linux and Windows installations on Uke.

OrangeFox remains the graphical recovery base and provides mature recovery primitives, but project-owned functionality should be implemented behind a strict native management layer. The UI must never directly execute destructive `dd`, `sgdisk`, `mkfs`, `ntfsresize`, `lpmake`, `cryptsetup` or Btrfs repair operations based on user-controlled paths. The same discovery, planning, validation, transaction and verification engine should serve the OrangeFox UI, recovery terminal, ADB, SSH and automated tests.

The core design principle is:

```text
DISCOVER
   ↓
IDENTIFY
   ↓
DIAGNOSE
   ↓
PLAN
   ↓
BACK UP
   ↓
REVALIDATE
   ↓
EXECUTE
   ↓
READ BACK / VERIFY
   ↓
COMMIT
   ↓
RECORD RECOVERY PATH
```

Unknown layouts, stale plans, unknown firmware, active snapshot merges, missing backups, ambiguous partition identities or unverifiable boot targets must fail closed.

The recovery should eventually provide four major roles:

1. **System recovery** — repair Android, Linux and Windows without booting them.
2. **Storage workstation** — safe GPT, filesystem, encryption, subvolume and snapshot administration.
3. **Boot control environment** — one-shot reboot to Android, Linux or Windows without requiring an interactive boot manager screen.
4. **Development/debug environment** — diagnose recovery, kernel, display, touch, USB, UFS, encryption, boot and OS failures locally or over ADB/SSH.

---

## 1. Current Repository Baseline

The current `MCC45TR/orangefox_device_xiaomi_uke` repository already has useful foundations that should be preserved rather than replaced.

The following source, host-test and package foundations are recorded in the
current repository. Their device behavior remains untested:

- Uke-specific device tree rather than blindly reusing Nabu offsets.
- Separate native `uke-recoveryctl`.
- Project-owned native installer.
- Exact partition label and PARTUUID-based selection.
- Read-only Linux/ESP mount planning.
- A/B awareness.
- Firmware-profile verification.
- Recovery image size verification.
- Hash and read-back verification.
- Stock boot-stack checks.
- Explicit preservation of inactive stock recovery in the safer installer path.
- ext4/FAT and GPT utilities already considered.
- Btrfs explicitly blocked when the stock recovery kernel lacks `CONFIG_BTRFS_FS`.
- Android FBE/decryption explicitly disabled until the real KeyMint/TEE path is validated.
- Host fixture testing and negative tests.
- Privacy and release gates.
- Separation of build success from physical hardware success.

These are important design choices. Future features should extend the same model instead of adding unguarded shell scripts.

---

## 2. Product Definition

A useful name for the project layer is:

**UKE Recovery Environment (URE)**

OrangeFox is the graphical recovery base, while URE is the device-specific management platform.

Conceptually:

```text
OrangeFox UI
   │
   ├──────── File Manager / Terminal / Standard Recovery Features
   │
   ▼
UKE Recovery UI Extensions
   │
   ▼
libuke-recovery
   │
   ├── Device Identity
   ├── Firmware Identity
   ├── Storage Graph
   ├── Transaction Engine
   ├── Crypto Manager
   ├── Filesystem Manager
   ├── Android Manager
   ├── Linux Rescue Manager
   ├── Windows Rescue Manager
   ├── Boot Target Manager
   ├── Network Rescue Manager
   ├── Backup / Restore
   ├── Diagnostics Engine
   └── Report / Audit Engine
         │
         ├── uke-recoveryctl
         ├── OrangeFox GUI adapter
         ├── ADB
         └── SSH
```

---

## 3. Non-Negotiable Safety Principles

### 3.1 Identity before path

Never trust `/dev/block/sda17` because a donor recovery used it.

A partition identity should be composed from as much evidence as possible:

```text
physical device / UFS LUN
sector size
disk capacity
GPT disk GUID
partition type GUID
partition unique GUID / PARTUUID
partition label
start sector
end sector
size
slot relationship
firmware profile
filesystem UUID
```

A destructive command must operate on a resolved internal object, not an arbitrary path typed by the UI.

### 3.2 Discovery must be read-only

Discovery must not:

- replay journals unless explicitly requested
- activate arbitrary device-mapper targets
- mount writable
- change GPT metadata
- change Android slots
- change EFI variables
- create snapshots
- unlock encrypted storage without an explicit user action

### 3.3 No implicit destructive fallbacks

Examples of prohibited behavior:

```text
mount failed → format
boot target not found → rewrite GPT
filesystem check failed → run --repair
Android failed → erase metadata
Windows boot failed → recreate ESP
```

Every destructive fallback must be a separately displayed plan.

### 3.4 Same preflight for GUI and CLI

The OrangeFox GUI must not bypass checks available in `uke-recoveryctl`.

Likewise SSH must not expose a privileged shortcut around the project management layer unless the user intentionally enters engineering shell mode.

### 3.5 Stale-plan rejection

A plan created against storage state A must not execute if storage is now state B.

Before execution, verify again:

- disk GUID
- partition GUIDs
- ranges
- filesystem identifiers
- active Android slot
- Android snapshot/merge state
- encryption mapping state
- source artifact hashes
- free space
- mounted/open users

### 3.6 Backup is part of a write operation

For high-risk changes, backup is not an optional unrelated menu item. It is a phase of the transaction.

---

## 4. Proposed Source Architecture

New management code uses C++; upstream Linux and EDK II retain their C/assembly
requirements. The tree below is a proposed refactor, not an existing directory
inventory. Preserve the current `src/device/xiaomi/uke/recoveryctl/` and
`src/inventory/` behavior while extracting shared code. Host automation prefers
Bash; no project-owned Python or tablet Python dependency is introduced.

```text
src/
├── libuke/
│   ├── core/
│   │   ├── result.*
│   │   ├── error.*
│   │   ├── operation_id.*
│   │   ├── json.*
│   │   └── logging.*
│   │
│   ├── device/
│   │   ├── model.*
│   │   ├── sku.*
│   │   ├── firmware_profile.*
│   │   └── hardware_identity.*
│   │
│   ├── storage/
│   │   ├── block_device.*
│   │   ├── ufs.*
│   │   ├── gpt.*
│   │   ├── partition.*
│   │   ├── storage_graph.*
│   │   ├── mount.*
│   │   └── ownership.*
│   │
│   ├── transaction/
│   │   ├── plan.*
│   │   ├── validator.*
│   │   ├── journal.*
│   │   ├── executor.*
│   │   ├── rollback.*
│   │   └── verification.*
│   │
│   ├── crypto/
│   │   ├── luks.*
│   │   ├── bitlocker.*
│   │   ├── dmcrypt.*
│   │   └── secret_buffer.*
│   │
│   ├── filesystem/
│   │   ├── probe.*
│   │   ├── ext4.*
│   │   ├── btrfs.*
│   │   ├── f2fs.*
│   │   ├── ntfs.*
│   │   ├── fat.*
│   │   ├── exfat.*
│   │   └── xfs.*
│   │
│   ├── android/
│   │   ├── slots.*
│   │   ├── boot_chain.*
│   │   ├── avb.*
│   │   ├── dynamic_partitions.*
│   │   ├── virtual_ab.*
│   │   ├── ota.*
│   │   └── fbe.*
│   │
│   ├── linux/
│   │   ├── detect.*
│   │   ├── os_release.*
│   │   ├── kernels.*
│   │   ├── boot_entries.*
│   │   ├── initramfs.*
│   │   ├── chroot.*
│   │   ├── systemd.*
│   │   ├── package_db.*
│   │   └── rescue.*
│   │
│   ├── windows/
│   │   ├── detect.*
│   │   ├── ntfs.*
│   │   ├── wim.*
│   │   ├── bitlocker.*
│   │   ├── esp.*
│   │   ├── bcd.*
│   │   └── rescue.*
│   │
│   ├── boot/
│   │   ├── target.*
│   │   ├── boot_once.*
│   │   ├── android_backend.*
│   │   ├── efi_backend.*
│   │   ├── aloha_backend.*
│   │   └── history.*
│   │
│   ├── editor/
│   │   ├── document.*
│   │   ├── atomic_save.*
│   │   ├── metadata.*
│   │   └── validators.*
│   │
│   ├── network/
│   │   ├── usb_network.*
│   │   ├── wifi.*
│   │   ├── ssh.*
│   │   └── diagnostics.*
│   │
│   ├── diagnostics/
│   │   ├── recovery.*
│   │   ├── kernel.*
│   │   ├── pstore.*
│   │   ├── display.*
│   │   ├── touch.*
│   │   ├── usb.*
│   │   ├── storage.*
│   │   └── boot.*
│   │
│   └── report/
│       ├── collector.*
│       ├── redactor.*
│       └── archive.*
│
├── recoveryctl/
├── recovery-ui/
└── installer/
```

---

## 5. Unified Storage Graph

The storage graph is the foundation for Android/Linux/Windows support.

Conceptual example only: the measured stock Uke layout spans six LUNs, and
`uke_esp`, `uke_linux` and `uke_windows` are prospective labels, not existing
stock partitions. Discover actual parent LUNs and ranges from the selected
profile and live read-only inventory; do not derive offsets from this diagram.
See [Global stock layout](STOCK-LAYOUT.md) and [China stock layout](STOCK-LAYOUT-CN.md).

Example:

```text
UFS
├── LUN0
│   └── early firmware partitions
├── LUN1
│   └── ...
└── user storage LUN
    └── GPT
        ├── boot_a
        ├── boot_b
        ├── init_boot_a
        ├── init_boot_b
        ├── recovery_a
        ├── recovery_b
        ├── vendor_boot_a
        ├── vendor_boot_b
        ├── super
        │   ├── system_a
        │   ├── system_b
        │   ├── vendor_a
        │   └── ...
        ├── metadata
        ├── userdata
        ├── uke_esp
        ├── uke_linux
        └── uke_windows
```

Every block object should expose:

```text
stable object ID
kernel device path
sysfs identity
parent disk / LUN
partition index
GPT label
GPT type GUID
PARTUUID
start sector
sector count
byte size
filesystem type
filesystem UUID
encryption type
mapper name
mount points
OS ownership
slot
boot role
read/write state
open users
health flags
```

The UI should visualize this graph rather than listing only Linux device names.

---

## 6. Machine-Readable API

`uke-recoveryctl` should become a frontend to `libuke-recovery`.

Recommended command hierarchy:

```text
uke-recoveryctl
├── device
│   ├── info
│   └── firmware
├── storage
│   ├── inventory
│   ├── graph
│   ├── mounts
│   └── health
├── gpt
│   ├── inspect
│   ├── backup
│   ├── compare
│   ├── repair-plan
│   └── restore
├── crypto
│   ├── detect
│   ├── unlock
│   ├── lock
│   ├── luks
│   └── bitlocker
├── filesystem
│   ├── inspect
│   ├── check
│   ├── repair-plan
│   ├── resize-plan
│   └── format-plan
├── btrfs
│   ├── info
│   ├── subvolume
│   ├── snapshot
│   ├── scrub
│   ├── balance
│   ├── send
│   ├── receive
│   └── rescue
├── android
│   ├── info
│   ├── slots
│   ├── boot-chain
│   ├── avb
│   ├── super
│   ├── ota
│   └── diagnose
├── linux
│   ├── detect
│   ├── info
│   ├── kernels
│   ├── boot-entries
│   ├── chroot
│   ├── diagnose
│   └── repair-plan
├── windows
│   ├── detect
│   ├── info
│   ├── boot
│   ├── wim
│   ├── bcd
│   ├── diagnose
│   └── repair-plan
├── boot
│   ├── targets
│   ├── status
│   ├── once
│   ├── default
│   └── history
├── network
│   ├── status
│   ├── usb
│   ├── wifi
│   └── ssh
├── diagnose
│   ├── all
│   ├── recovery
│   ├── kernel
│   ├── display
│   ├── touch
│   ├── usb
│   ├── storage
│   └── boot
├── backup
├── restore
├── report
└── transaction
    ├── show
    ├── validate
    ├── execute
    ├── verify
    └── rollback
```

Every important read operation should support:

```bash
uke-recoveryctl linux info --json
```

Example result:

```json
{
  "schema": 1,
  "operation_id": "op-...",
  "result": "ok",
  "device": {
    "codename": "uke",
    "firmware_profile": "global-stock303"
  },
  "warnings": [],
  "data": {}
}
```

Human-readable output remains available, but UI integration should use a structured interface.

---

## 7. Transaction Engine

A storage-changing operation should have a serializable plan.

Example fields:

```json
{
  "schema": 1,
  "operation_id": "op-20260930-...",
  "operation": "filesystem.resize",
  "device_identity": {},
  "firmware_identity": {},
  "source_state": {},
  "target_state": {},
  "affected_partitions": [],
  "mount_requirements": [],
  "required_backups": [],
  "source_artifact_hashes": [],
  "snapshot_state": "none",
  "estimated_space": {},
  "expected_changes": [],
  "verification": [],
  "rollback": []
}
```

### 7.1 Transaction states

```text
CREATED
VALIDATED
BACKUP_STARTED
BACKUP_VERIFIED
READY
EXECUTING
VERIFYING
COMMITTED
ROLLBACK_REQUIRED
ROLLED_BACK
FAILED_SAFE
FAILED_UNCERTAIN
```

`FAILED_UNCERTAIN` should be a first-class state. The UI must not falsely report failure as “nothing changed” if a write may have partially occurred.

### 7.2 Power-loss design

For operations that permit it:

1. Record operation intent.
2. fsync transaction journal.
3. Store backup identity.
4. Execute bounded step.
5. fsync.
6. Store completed step.
7. Continue.
8. Verify.
9. Commit.

On next recovery boot:

```text
Incomplete storage transaction detected
Operation: ...
Last confirmed phase: ...
Current storage identity: ...
```

Offer:

```text
Resume if safe
Verify current state
Roll back
Export report
```

Do not blindly resume.

---

## 8. GPT and Partition Management

### 8.1 GPT inspection

Display:

- physical disk / UFS LUN
- capacity
- logical and physical sector size
- primary GPT validity
- backup GPT validity
- disk GUID
- each partition type GUID
- unique GUID
- label
- range
- alignment
- filesystem
- OS owner

### 8.2 GPT backup

Backup package:

```text
uke-gpt-backup/
├── manifest.json
├── lun0-primary.bin
├── lun0-backup.bin
├── lunN-primary.bin
├── lunN-backup.bin
├── partition-table.json
└── SHA256SUMS
```

Restore must reject:

- wrong capacity
- wrong LUN
- wrong device family
- ambiguous disk
- mismatched sector size
- incompatible firmware profile unless explicitly approved
- backup from an unrelated device

### 8.3 GPT repair

Operations:

- primary ↔ backup comparison
- CRC validation
- detect missing backup table
- detect geometry mismatch
- regenerate backup GPT only after a reviewed plan
- regenerate primary GPT only after a reviewed plan
- partition overlap detector
- out-of-range detector
- alignment warnings

No generic one-click `fixgpt`.

### 8.4 Partition layout designer

Profiles may include:

```text
Stock Android
Android + Fedora
Android + Windows
Android + Fedora + Windows
Fedora only user OS
Fedora + Windows
Custom
```

The profile is a **desired state**, not a collection of hard-coded offsets.

The planner calculates ranges from the real disk.

---

## 9. Filesystem Framework

Filesystems to target:

| Filesystem | Main use | Target support |
|---|---|---|
| F2FS | Android userdata/metadata | inspect, check, format/resize only when proven safe |
| EROFS | Android logical system partitions | read-only inspect/mount |
| ext4 | Linux/Android | full rescue and resize |
| Btrfs | Linux/Fedora | full subvolume/snapshot/rescue |
| FAT16/32 | ESP | inspect/check/format |
| exFAT | removable media | inspect/check/format |
| NTFS | Windows | inspect/mount/resize/limited repair |
| XFS | optional Linux | inspect/check/repair where viable |

### 9.1 Common filesystem state

Expose:

```text
filesystem
UUID
label
size
used
free
features
dirty state
mount state
read/write state
last check
reported errors
supported operations
```

### 9.2 Filesystem policy levels

```text
SAFE READ-ONLY
SAFE MAINTENANCE
WRITE WITH BACKUP
EXPERT / HIGH RISK
UNSUPPORTED
```

Each UI action receives a policy level.

---

## 10. Linux System Discovery

This should be an automatic Linux profile generator.

Discovery chain:

```text
Detect Linux partition
   ↓
Detect encryption
   ↓
Unlock if user requests
   ↓
Detect filesystem
   ↓
Detect Btrfs root subvolume if applicable
   ↓
Read os-release
   ↓
Detect architecture
   ↓
Detect package database
   ↓
Enumerate kernels
   ↓
Enumerate initramfs images
   ↓
Enumerate module trees
   ↓
Enumerate BLS / EFI entries
   ↓
Match kernel ↔ initramfs ↔ modules ↔ DTB ↔ boot entry
   ↓
Create Linux system profile
```

### 10.1 Distribution detection

Primary:

```text
/etc/os-release
/usr/lib/os-release
```

Use `/etc/os-release` when present, falling back to `/usr/lib/os-release`;
parse bounded data without sourcing or executing the file.

Fallbacks:

```text
/etc/fedora-release
/etc/redhat-release
/etc/debian_version
/etc/arch-release
/etc/alpine-release
/etc/gentoo-release
```

Package databases may provide secondary evidence:

```text
RPM
dpkg
pacman
apk
```

UI example:

```text
Linux Installation

Distribution     Fedora Linux
Variant          Rawhide
Version          rolling (illustrative)
Architecture     aarch64
Detection        high confidence

Filesystem       Btrfs
Root subvolume   @root
Encryption       LUKS2

Default kernel   7.2.8-senemos.uke
Boot system      BLS / UEFI
```

### 10.2 Kernel discovery

Inspect:

```text
/usr/lib/modules/*
/lib/modules/*
/boot/vmlinuz-*
/boot/Image*
/boot/initramfs-*
/boot/initrd-*
/boot/loader/entries/*
ESP boot entries
UKIs where present
```

Do not assume `uname -r` identifies the installed system. In recovery it identifies the recovery kernel.

Kernel consistency matrix:

```text
Kernel                    Image  Initramfs  Modules  Boot Entry  DTB
7.2.8-senemos.uke         OK     OK         OK       OK          OK
7.2.7-senemos.uke         OK     OK         OK       OK          OK
old-kernel                 OK     MISSING    OK       STALE       ?
```

Potential automatic diagnoses:

- selected BLS entry references missing initramfs
- kernel image has no matching module tree
- root PARTUUID changed
- Btrfs root subvolume in kernel arguments does not exist
- encrypted root exists but initramfs lacks required crypt configuration
- referenced DTB missing
- ESP entry stale
- kernel package database disagrees with files on disk

---

## 11. Linux Rescue Environment

Linux should be recoverable even if it cannot boot.

### 11.1 Core functions

- mount root read-only
- unlock root
- mount root read/write after explicit confirmation
- mount `/boot`
- mount ESP
- open file manager
- open GUI text editor
- open recovery shell
- enter controlled chroot
- inspect journal
- inspect systemd state offline
- inspect installed kernels
- repair boot configuration
- rebuild initramfs
- restore individual configuration files
- roll back Btrfs snapshots
- verify package database
- perform distro-aware diagnostics

### 11.2 Controlled chroot

Chroot is subject to the project's no-tablet-Python policy. Inspect the target
rootfs, selected repair tools, interpreters and transitive dependencies before
execution. A host-only build exception does not permit Python in a tablet
chroot; defer a repair that cannot satisfy the target execution policy.

Before chroot:

```text
root mounted
/boot mounted where applicable
ESP mounted where applicable
/dev bind
/dev/pts bind
/proc mounted
/sys mounted
/run prepared/bound
DNS policy selected
```

When leaving:

- find processes still using target mounts
- terminate only with explicit confirmation
- unmount nested mounts in reverse order
- close mapper if no longer needed
- report cleanup failures

GUI should visually distinguish:

```text
RECOVERY SHELL
```

from:

```text
FEDORA CHROOT
```

---

## 12. Native GUI Text Editor

A built-in editor is a major Linux rescue feature.

### 12.1 Required editor features

- line numbers
- UTF-8
- search
- replace
- go to line
- undo/redo
- word wrap
- optional syntax highlighting
- read-only mode
- save as
- external-change detection
- file-size limit for safe GUI editing
- binary-file rejection
- backup before save

Suggested syntax support:

```text
fstab
crypttab
systemd units
systemd mount units
BLS entries
loader.conf
GRUB config
INI
JSON
YAML
TOML
shell
udev rules
modprobe config
dracut config
NetworkManager profiles
```

### 12.2 Metadata-aware saving

Saving must preserve or intentionally update:

- owner
- group
- Unix mode
- ACL
- xattrs
- SELinux context
- file capabilities
- symlink policy

Safe save:

```text
read metadata
write temporary file
fsync temporary file
run file-type validator when available
apply required metadata
atomic rename
fsync containing directory
verify content hash
```

### 12.3 Smart configuration validation

Examples:

`fstab`:
- syntax
- UUID/PARTUUID existence
- duplicate mount points
- unknown filesystem
- missing Btrfs subvolume
- invalid mount options where detectable

`crypttab`:
- UUID exists
- encryption type matches
- mapper names are unique

BLS:
- kernel file exists
- initramfs exists
- options reference valid root
- DTB exists when required

---

## 13. Linux File Manager

The file manager should understand Unix metadata.

Display:

- UID/GID and resolved names when possible
- mode
- ACL presence
- SELinux context
- xattrs
- capabilities
- symlink target
- filesystem
- subvolume/snapshot
- immutable/read-only flags where relevant

Copy/move operations should optionally preserve all supported metadata.

Snapshot-aware browsing:

```text
Current root
Snapshot: pre-update
```

Possible actions:

```text
Compare file
Restore file
Restore directory
Open both
```

---

## 14. LUKS / dm-crypt Support

Target:

- LUKS1 detection
- LUKS2 detection
- unlock by passphrase
- mapper close
- read metadata
- keyslot inspection
- token inspection
- LUKS header backup
- LUKS header restore with strong safeguards
- add/change/remove passphrase only in advanced mode
- re-encryption only much later and only after extensive fixture/device tests

### 14.1 Secret handling

Never place secrets in:

- process argv
- recovery logs
- JSON output
- crash reports
- shell history
- persistent temporary files

Preferred design:

- secure input widget
- pipe/fd/API based secret passing
- memory locking if practical
- explicit buffer zeroing
- short secret lifetime
- mapper close on lock

### 14.2 Auto-lock options

Optional:

```text
lock encrypted volumes on recovery screen lock
lock after inactivity
lock before reboot
lock before switching to mass-storage mode
```

---

## 15. Windows BitLocker Access

Use cryptsetup BITLK support where kernel/userspace support is validated.

Target unlock methods:

- BitLocker password
- BitLocker recovery passphrase
- startup key file
- explicit volume key only as an expert workflow

Do not promise:

- TPM-bound automatic unlock
- SmartCard unlock
- arbitrary protector modification

Initial BitLocker scope should be **unlock/read/write access to an existing supported volume**, not editing BitLocker headers.

UI:

```text
Windows Volume
Encryption      BitLocker
State           Locked

Unlock using
[ Password ]
[ Recovery Key ]
[ Startup Key ]
```

Secrets follow the same no-log policy as LUKS.

---

## 16. Comprehensive Btrfs Manager

Btrfs support should be treated as a major subsystem, not just a mount option.

A compatible recovery kernel with Btrfs enabled is required. The current measured stock kernel profile where `CONFIG_BTRFS_FS` is disabled must continue to report Btrfs as unavailable rather than faking support.

### 16.1 Btrfs filesystem information

Display:

- filesystem UUID
- devices
- total/used allocation
- data profile
- metadata profile
- system profile
- device errors
- generation
- incompat/compat features
- mounted state

### 16.2 Subvolume manager

Functions:

- list
- create
- delete
- rename where safely implementable
- inspect UUID
- parent UUID
- generation
- read-only property
- default subvolume
- nested hierarchy

UI:

```text
ID     Path                         RO
256    @root                        no
257    @home                        no
258    @var                         no
300    @snapshots/pre-update        yes
```

### 16.3 Snapshot manager

- create RW snapshot
- create RO snapshot
- delete snapshot
- inspect snapshot parent
- compare snapshot
- mount snapshot read-only
- restore individual files
- restore directories
- prepare rollback
- boot snapshot once
- set snapshot as future Linux boot target

Avoid presenting a generic “activate snapshot” abstraction without explaining what changes. Depending on boot design, snapshot selection may involve Btrfs default-subvolume state or boot-entry rootflags.

### 16.4 Safe rollback workflow

```text
selected rollback snapshot
   ↓
validate snapshot
   ↓
snapshot current root as rescue point
   ↓
prepare new boot target
   ↓
validate kernel/initramfs/rootflags
   ↓
set one-shot Linux boot
   ↓
reboot
```

Prefer a one-shot boot before making a permanent default change.

### 16.5 Scrub

- start
- status
- pause/cancel where supported
- result summary
- device error reporting

### 16.6 Balance

- filtered balance only by default
- data and metadata filters
- status
- pause/cancel
- estimate affected data when practical
- prevent casual full balance

### 16.7 Send/receive

Support:

- full snapshot send
- incremental send with parent
- receive to external disk
- receive to another Btrfs filesystem
- stream to host over SSH/ADB
- stream checksum
- progress
- resume strategy at the recovery-manager layer where feasible

Read-only snapshots should be used for reliable send semantics.

### 16.8 Advanced rescue

Expose only with strong warnings and policy gates:

- inspect superblocks
- inspect chunk/tree structures
- superblock rescue workflows
- log-tree related rescue where justified
- chunk recovery where supported
- read-only `btrfs check`

`btrfs check --repair` must never be a normal Repair button. It belongs in an expert/high-risk path after backup and unmounted-filesystem checks.

---

## 17. ext4 Manager

Functions:

- detect
- mount RO
- mount RW
- read superblock info
- filesystem UUID/label
- `e2fsck`
- resize planning
- `resize2fs`
- tune2fs-backed metadata operations
- create filesystem
- journal information
- bad-state detection
- image/fixture testing

Before resize:

- unmount requirements
- partition range validation
- minimum filesystem size
- target capacity
- backup presence
- post-resize verification

---

## 18. F2FS Manager

Important because Android Uke userdata/metadata may use F2FS.

Target tools may include:

```text
fsck.f2fs
dump.f2fs
mkfs.f2fs
resize.f2fs
```

Do not enable resize/format merely because binaries exist.

Required tests:

- exact feature flags
- encryption-related metadata
- Android FBE interaction
- donor claims of broken F2FS formatting
- damaged image fixtures
- interrupted operations
- mounted-device rejection

---

## 19. FAT/exFAT Manager

Needed for ESP and removable media.

Functions:

- inspect
- fsck
- format
- label
- mount
- ESP-specific validation

ESP validation should recognize expected EFI directory structure but never delete “unknown” EFI applications automatically.

---

## 20. NTFS Manager

Functions:

- filesystem inspection
- RO mount
- RW mount after explicit approval
- preliminary repair with `ntfsfix` where appropriate
- `ntfsresize`
- `mkntfs`
- volume usage
- BitLocker-before-NTFS layering

The UI must clearly state that `ntfsfix` is not equivalent to a full Windows `chkdsk`.

---

## 21. Windows Detection

Detect a Windows installation using multiple indicators:

```text
NTFS volume
/Windows/System32
Windows registry hives
EFI/Microsoft/Boot
BCD
recovery environment files
BitLocker metadata
```

UI:

```text
Windows Installation

Edition          Windows 11 ARM64
Partition        uke_windows
Filesystem       NTFS
Encryption       BitLocker
ESP              uke_esp
Boot files       detected
BCD              detected
```

Edition/build detection should use reliable offline metadata and should report unknown rather than guessing.

---

## 22. WIM/ESD Support

Package `wimlib` or a carefully reviewed subset/build.

Functions:

- inspect WIM/ESD
- list images/indexes
- verify archive
- show edition metadata
- extract individual files
- apply image to target NTFS
- capture volume to WIM
- split WIM handling if supported
- stream input where appropriate
- hash source archive
- verify target capacity

For Windows deployment on Linux-like recovery, direct application to an unmounted NTFS block volume is preferable when Windows metadata preservation is required.

WIM apply is not the entire Windows installation process. ESP/boot configuration is a separate phase.

---

## 23. Windows Boot Rescue

Initial safe features:

- detect Microsoft EFI boot files
- inspect BCD
- back up BCD
- back up `EFI/Microsoft`
- validate boot file existence
- detect stale partition references where parsable
- restore a known-good project-created backup
- compare current vs backup

Later:

- native BCD parser/editor
- rebuild selected entries
- create controlled Windows boot profile

Do not blindly rewrite BCD as the first implementation.

---

## 24. Android Manager

### 24.1 Device/firmware status

Display:

```text
Model
SKU
Region
Firmware
Android version
Bootloader state
Current slot
Slot A state
Slot B state
Snapshot/merge state
Recovery profile compatibility
```

### 24.2 Boot chain

Inspect and optionally back up:

```text
boot
init_boot
vendor_boot
dtbo
vbmeta
vbmeta_system
recovery
```

Show:

- size
- SHA-256
- AVB descriptors
- rollback metadata where relevant
- slot association
- expected firmware-profile hash when known

### 24.3 A/B controls

Read-only first:

- number of slots
- current slot
- bootable
- successful
- retry state
- snapshot merge state

Write support later:

- set active Android slot
- only after snapshot/OTA gates
- explicit rollback route
- interruption tests

### 24.4 Dynamic partitions

Support:

- `lpdump`
- logical partition map
- super metadata slots
- current logical sizes
- mounted state
- COW/snapshot conflict detection

Write operations:

- resize
- create/delete logical partition
- rebuild metadata

Only through transaction plans.

### 24.5 OTA

- sideload
- inspect payload metadata
- validate target firmware
- reject path traversal in extracted artifacts
- slot-aware operation
- Virtual A/B state checks
- interrupted OTA diagnostics

### 24.6 Android FBE

Future gated feature.

Required before claiming support:

- installed firmware-specific fstab
- wrapped-key mode
- KeyMint/TEE services
- gatekeeper/weaver dependencies where applicable
- real credential unlock test
- read-only successful data access
- no data formatting side effects

---

## 25. Direct Reboot to Android / Linux / Windows

The recovery UI should expose:

```text
Reboot
├── Android
├── Linux
├── Windows
├── Recovery
├── Bootloader
└── Power Off
```

The user should not be required to interact with a boot-manager menu.

### 25.1 Important architecture rule

“Without a boot manager” should mean **without an interactive boot menu**.

A low-level routing mechanism is still required if the stock Android bootloader cannot directly represent Fedora/Windows as native targets.

### 25.2 Boot Target Manager

API:

```bash
uke-recoveryctl boot targets
uke-recoveryctl boot status
uke-recoveryctl boot once android
uke-recoveryctl boot once linux
uke-recoveryctl boot once windows
uke-recoveryctl boot default android
uke-recoveryctl boot default linux
uke-recoveryctl boot default windows
```

### 25.3 Possible backends

#### Backend A — stock/native bootloader route

Use only if Uke firmware exposes a proven native mechanism.

#### Backend B — UEFI one-shot entry

Where Aloha or another UEFI environment follows a compatible model, write a one-shot target such as an EFI `BootNext`/loader one-shot entry.

#### Backend C — Aloha one-shot request

Example project-owned file:

```text
/EFI/UKE/next-boot.json
```

Example:

```json
{
  "schema": 1,
  "target": "linux",
  "entry": "fedora-mainline",
  "request_id": "...",
  "one_shot": true
}
```

Aloha:

1. reads
2. validates
3. consumes/deletes
4. chainloads target
5. falls back safely if invalid

This allows a direct button without showing a boot menu.

### 25.4 One-shot first

`Reboot to Fedora` should normally mean:

> boot Fedora exactly once, then revert to the configured default.

This avoids persistent boot loops after a broken Linux update.

### 25.5 Boot preflight

Linux:

- root partition available
- encryption policy understood
- filesystem valid enough to boot
- selected subvolume exists
- kernel exists
- initramfs exists
- DTB exists where required
- ESP entry exists

Windows:

- NTFS/BitLocker state understood
- Windows root exists
- Microsoft EFI loader exists
- BCD is readable

Android:

- selected slot known
- snapshot/merge state safe
- boot chain exists
- slot metadata coherent

---

## 26. Boot History and Failed-Boot Recovery

Maintain a small project-owned boot request history, not user content.

Example:

```text
16:20 Android   request    completed/unknown
16:42 Fedora    request    failed-or-returned
16:44 Recovery  entered
```

Record:

- request ID
- target
- timestamp
- expected entry
- one-shot/default
- recovery re-entry
- pstore correlation if available

On recovery startup:

```text
Previous requested target: Fedora
Recovery entered again
Kernel crash record found: yes
```

Offer:

```text
Diagnose Linux
View crash log
Boot previous known-good target
```

---

## 27. Project Aloha Integration

Aloha should become the low-level UEFI routing layer where needed, not necessarily a menu the user must see.

Recovery/Aloha contract should include:

- versioned boot request schema
- explicit target IDs
- Linux entry IDs
- Windows entry IDs
- Android return contract
- recovery return contract
- request consumption rules
- timeout/fallback
- invalid-request behavior
- boot attempt status where implementable

Aloha must reject unknown request versions and malformed targets.

---

## 28. Network Rescue

SSH is worth implementing, but should not block early recovery functionality.

Priority:

```text
P1 USB networking + SSH/SFTP
P2 Wi-Fi + SSH/SFTP
```

### 28.1 USB networking

Preferred first network path.

Possible gadget modes depend on platform support:

- USB NCM
- ECM where host support makes sense
- RNDIS only where needed
- ADB may coexist only after tested gadget-composition behavior

Example:

```text
Recovery IP: 192.168.42.1
Host IP:     192.168.42.2
```

### 28.2 SSH server

A small server such as Dropbear is attractive for recovery, provided source, licensing and cryptographic configuration are reviewed.

Default:

```text
SSH disabled
password login disabled
root password login disabled
public-key authentication enabled
```

UI:

```text
Network Rescue

USB Network       ON
SSH               ON
SFTP              ON

Address           192.168.42.1
Port              22
Host Key          ED25519
Fingerprint       SHA256:...
Connected Clients 1
```

### 28.3 SSH key import

Methods:

- ADB push
- file picker
- USB drive
- QR/public-key input later if useful

Do not persist an imported key unless the user explicitly chooses to.

### 28.4 SFTP/SCP

Use cases:

- recover `/home`
- fetch logs
- edit files from workstation
- upload kernels
- upload WIM images
- transfer backups
- transfer diagnostic bundles

### 28.5 Wi-Fi recovery

Later feature:

- firmware loading
- interface discovery
- regulatory handling
- network scan
- WPA2/WPA3 support as practical
- DHCP
- static IP
- credential secrecy
- disconnect/forget

Wi-Fi credentials must not be included in diagnostic reports.

---

## 29. Host-Assisted Rescue

Large backups should not require equal free space on the tablet.

Potential flow:

```text
tablet block/file stream
     ↓ USB / ADB / SSH
host receiver
     ↓
chunk verification
     ↓
backup manifest
```

Features:

- partition streaming
- sparse-aware streaming where reliable
- per-chunk hashes
- final SHA-256
- progress
- reconnect/resume at chunk boundaries
- compression optional
- backup manifest

Restore:

- identify target first
- validate image manifest
- reject wrong target
- stream
- read back selected ranges/full hash as appropriate

---

## 30. Backup Framework

Backup types:

### 30.1 Android

- recovery
- boot chain
- selected raw partitions
- logical partition metadata
- GPT
- userdata using appropriate strategy when unlocked

### 30.2 Linux

- file backup
- Btrfs snapshot
- Btrfs send stream
- filesystem image
- kernel/boot backup
- ESP
- LUKS header

### 30.3 Windows

- WIM capture
- raw partition image
- ESP/Microsoft boot backup
- BCD
- BitLocker metadata information, without leaking secrets

### 30.4 Backup manifest

Always record:

```text
schema
device
firmware
disk GUID
partition identity
filesystem
encryption state
creation time
tool versions
source sizes
hashes
compression
restore requirements
```

---

## 31. Diagnostics Engine

Recovery should be part of the debugging workflow.

Top-level modules:

```text
Recovery
Kernel
Boot
Display
Touch
Input
USB
UFS/Storage
Filesystems
Encryption
Android
Linux
Windows
Network
Power
Thermal
Battery/Charging
```

---

## 32. Recovery Boot Stage Tracing

Add explicit boot milestones:

```text
R000 kernel entered
R010 init started
R020 base mounts ready
R030 vendor/module path ready
R040 modules loading
R050 display probe
R060 touch/input probe
R070 USB gadget
R080 recovery services
R090 OrangeFox UI initialization
R100 UI ready
```

For each stage:

- monotonic timestamp
- wall clock if available
- status
- errno
- service
- module
- dependency
- retry count

If recovery stalls, persisted crash-safe evidence should identify the last stage where practical.

---

## 33. Pstore / ramoops

At startup inspect:

```text
/sys/fs/pstore
```

If records exist:

```text
Previous boot crash detected
Type: kernel panic/watchdog/console record
```

Actions:

- view
- export
- correlate with boot request
- preserve before clearing
- clear only explicitly

Pstore support depends on the actual kernel/device memory reservation configuration.

---

## 34. Kernel and Module Diagnostics

Display:

```text
kernel release
kernel build ID
command line
config hash if available
loaded modules
module vermagic
module dependencies
failed module loads
firmware requests
deferred probes
taints
```

Uke-specific checks:

- touch module readiness
- display/DRM
- USB
- storage
- charging
- required firmware blobs
- stock kernel/module profile matching

Example:

```text
xiaomi_touch.ko
present       yes
vermagic      match
dependencies  satisfied
loaded        no
last error    probe deferred
```

---

## 35. Display Diagnostics

Functions:

- identify DRM/fb devices
- connector/panel status
- mode
- resolution
- refresh rate
- brightness path
- brightness range
- rotation state
- render test patterns
- screenshot if safe
- test 0/90/180/270 mapping
- capture DRM errors

Test screens:

- black
- white
- RGB
- gradients
- checkerboard
- touch/grid alignment overlay

---

## 36. Touch/Input Diagnostics

Display:

- `/dev/input/event*`
- name
- bus/vendor/product/version
- capabilities
- ABS ranges
- pressure
- slots
- event rate

Interactive page:

```text
Finger 1
x=...
y=...
pressure=...
slot=...
```

Features:

- edge test
- multi-touch test
- dead-area map
- coordinate rotation verification
- stylus basic event inspection
- hardware-key events

Do not store touch traces persistently unless explicitly exported.

---

## 37. USB Diagnostics

Display:

- UDC
- gadget composition
- state
- USB role
- ADB
- MTP
- fastbootd
- mass storage
- USB networking
- host connection

Actions:

- restart gadget safely
- switch supported mode
- export logs
- test enumeration
- transfer checksum test

Mass-storage rule:

> A block device exposed to the host must not remain locally writable/mounted.

---

## 38. UFS / Storage Diagnostics

Display where interfaces exist:

- storage model/revision
- capacity
- LUN layout
- block errors
- I/O errors
- lifetime/health fields if exposed
- read-only state
- kernel storage errors
- GPT validity

Do not invent SMART-like metrics if UFS does not expose them through the available kernel interface.

---

## 39. Linux Boot Diagnosis

One-button analysis:

```text
Diagnose Linux Boot
```

Possible output:

```text
[ OK ] GPT / Linux partition
[ OK ] LUKS2 header
[ OK ] Btrfs filesystem
[ OK ] @root subvolume
[ OK ] os-release
[ !! ] /etc/crypttab references missing UUID
[ OK ] kernel image
[ !! ] initramfs missing expected crypto support
[ OK ] EFI entry
```

The engine should distinguish:

- observation
- likely consequence
- suggested next test
- repair options

Do not present uncertain guesses as proven root cause.

---

## 40. Windows Boot Diagnosis

Checks:

- Windows root detection
- BitLocker state
- NTFS state
- ESP
- `bootmgfw.efi`
- BCD readability
- referenced volume identities
- recovery environment files
- free space

Prefer inspection and backup before modification.

---

## 41. Android Boot Diagnosis

Checks:

- slot state
- snapshot/merge
- boot/init_boot/vendor_boot/dtbo existence
- AVB descriptors
- super metadata
- expected firmware profile
- recovery compatibility
- module/kernel mismatch clues

---

## 42. Offline Logs

Linux:

- systemd journal
- previous boots
- kernel logs stored by Linux
- failed unit data where available

Windows:

- selected offline logs can be surfaced later if parsers are trustworthy
- initial scope may simply expose relevant files through file manager

Android:

- recovery logs
- pstore
- logcat when services are available
- last-kmsg where applicable

---

## 43. Diagnostic Report Export

Button:

```text
Generate Diagnostic Report
```

Example:

```text
uke-report-20260930-161200.tar.zst
```

Contents:

```text
manifest.json
device.json
firmware.json
storage.json
gpt.json
mounts.json
boot.json
android.json
linux.json
windows.json
network.json
kernel.txt
modules.txt
dmesg.txt
pstore/
usb.txt
input.txt
display.txt
recovery.log
```

Automatic redaction targets:

- serial numbers
- Wi-Fi credentials
- Wi-Fi SSIDs if privacy policy requires
- MAC addresses unless explicitly included
- ADB host identity
- passwords
- recovery keys
- private file contents
- personal filenames/paths when not needed
- home-directory build paths
- host build identity

Offer:

```text
Public scrubbed report
Private full report
```

The second requires explicit user confirmation.

Both exports must exclude credentials, passphrases, recovery keys and signing
material. Private reports may retain explicitly selected diagnostic identifiers
and paths locally; they are never publication inputs. Public redaction needs
negative fixtures and a reviewed export, not only a filename change.

---

## 44. Recovery Safe Mode

Provide a minimal recovery boot mode.

Safe mode policy:

- minimal modules
- no automatic internal writable mounts
- no MTP
- no optional HALs
- no automatic decryption
- ADB diagnostics
- basic display path if possible
- conservative USB configuration
- engineering logs

Use case:

```text
normal recovery fails
safe recovery starts
→ compare module/service stages
```

---

## 45. Safe vs Engineering Builds

Recommended release profiles:

### Safe/Public

- standard OrangeFox recovery
- read-only diagnostics
- approved repair operations
- backup/restore
- verified installer
- boot target controls
- file editor
- Linux rescue
- protected filesystem operations

### Engineering

Additionally:

- direct advanced tools
- verbose tracing
- device-mapper controls
- experimental filesystem tools
- unproven boot targets
- raw inspection utilities
- expert GPT operations

A stable/public UI should not expose an unguarded `sgdisk` button merely because `sgdisk` exists in the ramdisk.

---

## 46. Recovery UI Information Architecture

Proposed top-level navigation:

```text
HOME

Install
Backup
Restore

Systems
├── Android
├── Linux
└── Windows

Storage
├── Overview
├── Partitions
├── GPT
├── Filesystems
├── Encryption
├── Btrfs
└── Device Mapper

Boot
├── Reboot to Android
├── Reboot to Linux
├── Reboot to Windows
├── Boot Profiles
├── Recovery
└── Bootloader

Rescue
├── Automatic Diagnosis
├── Android Repair
├── Linux Repair
├── Windows Repair
├── Boot Repair
└── Filesystem Repair

Files
├── File Manager
├── Text Editor
├── Compare
└── Terminal

Network
├── ADB
├── USB Networking
├── Wi-Fi
├── SSH
├── SFTP
└── Diagnostics

Diagnostics
├── Recovery
├── Kernel
├── Display
├── Touch/Input
├── USB
├── Storage
├── Power/Thermal
├── Android
├── Linux
├── Windows
└── Export Report

Advanced
├── Transactions
├── Dynamic Partitions
├── AVB
├── Boot Images
├── Chroot
├── Device Mapper
└── Engineering Tools
```

---

## 47. Home Screen Status Cards

Useful cards:

```text
Device
POCO Pad X1 / Uke
Firmware profile: ...

Android
Slot A
Snapshot: none

Linux
Fedora Rawhide
Kernel 7.2.8-senemos.uke
Btrfs / LUKS2

Windows
Windows 11 ARM64
BitLocker: locked

Storage
476.9 GiB
GPT healthy

Network
ADB connected
SSH off
```

Only detected systems should be shown.

---

## 48. Repair Center Design

Every repair module follows:

```text
Diagnose
   ↓
Explain
   ↓
Generate Plan
   ↓
Show exact changes
   ↓
Back up
   ↓
Execute
   ↓
Verify
```

Avoid opaque buttons like:

```text
FIX BOOT
FIX GPT
FIX BTRFS
```

without explaining the actual planned mutation.

---

## 49. Operation Risk Levels

UI badges:

```text
READ ONLY
LOW RISK
MODIFIES FILES
MODIFIES FILESYSTEM
MODIFIES PARTITION TABLE
MODIFIES BOOT CHAIN
EXPERT / HIGH RISK
```

This gives the user a consistent mental model.

---

## 50. Package / Utility Targets

Potential recovery payloads, depending on source/license/build feasibility:

### Core

```text
bash
toybox
sgdisk
lpdump/lptools
dmsetup
blkid/libblkid equivalent
```

### ext4

```text
e2fsck
mke2fs
resize2fs
tune2fs
```

### F2FS

```text
fsck.f2fs
dump.f2fs
mkfs.f2fs
resize.f2fs
```

### Btrfs

```text
btrfs
btrfs-progs
```

### FAT/exFAT

```text
fsck.fat
mkfs.fat
fsck.exfat
mkfs.exfat
```

### NTFS

```text
mkntfs
ntfsfix
ntfsresize
required libntfs-3g components
```

### Crypto

```text
cryptsetup
device-mapper dependencies
```

### Windows image

```text
wimlib-imagex
```

### Network

```text
small SSH server such as Dropbear
SFTP support
DHCP client/server components as required
Wi-Fi control stack later
```

Every package needs:

- exact source pin
- license
- build recipe
- target architecture check
- shared-library closure
- no accidental Python runtime
- payload privacy scan
- version record in report

---

## 51. Recovery Kernel Requirements

The recovery kernel should eventually be project-owned or otherwise explicitly matched to the selected firmware and feature set.

Candidate configuration requirements include:

```text
Btrfs
device mapper
dm-crypt
kernel crypto algorithms needed by LUKS/BITLK
NTFS3 if selected
ext4
F2FS
FAT/VFAT
exFAT
overlay/supporting VFS features as needed
USB gadget
USB networking
pstore/ramoops if platform-reserved memory supports it
required UFS/storage drivers
touch/display modules or built-ins
network/WLAN dependencies for Wi-Fi phase
```

Do not enable a filesystem only in userspace if the kernel cannot mount it.

Kernel capability discovery should be surfaced in the UI:

```text
Btrfs      unavailable: kernel driver missing
BitLocker  unavailable: required crypto support missing
Wi-Fi      unavailable: firmware/driver not loaded
```

---

## 52. Boot Configuration Standards

Where Linux boot infrastructure is compatible, support Boot Loader Specification concepts.

Detect:

- Type #1 loader entries
- UKIs / Type #2 style artifacts where relevant
- entry IDs
- default
- one-shot target

Recovery should not depend exclusively on systemd-boot. Aloha may provide its own compatible Uke contract.

---

## 53. Linux Kernel Manager

Page:

```text
Linux → Kernels
```

Display:

```text
7.2.8-senemos.uke
DEFAULT
Kernel       OK
Initramfs    OK
Modules      OK
DTB          OK
Boot entry   OK

7.2.7-senemos.uke
Kernel       OK
Initramfs    OK
Modules      OK
DTB          OK
```

Functions:

- inspect
- boot once
- set default
- rebuild initramfs
- inspect command line
- restore previous kernel
- remove obsolete kernel only after backup/snapshot gate

---

## 54. Fedora-Specific Rescue

Because Fedora is the primary Linux target, optional Fedora-aware functionality is justified.

Functions:

- detect RPM database
- verify selected packages
- inspect installed kernel RPMs
- inspect dracut configuration
- rebuild initramfs in chroot
- inspect BLS entries
- validate SELinux labels
- schedule/perform relabel when appropriate
- inspect failed offline journal
- inspect systemd presets/units
- detect broken `/etc/fstab`
- detect broken `/etc/crypttab`

Do not make Fedora awareness prevent generic Linux rescue.

---

## 55. Package Database Read-Only Inspection

Support:

- RPM
- dpkg
- pacman
- apk

Potential uses:

- identify distro
- determine installed kernel packages
- find package owning a file
- verify expected config/package version
- identify incomplete update

Write/package repair should usually happen inside the controlled target chroot rather than implementing multiple package managers in recovery.

---

## 56. External USB Storage

Support removable storage for:

- backups
- recovery reports
- WIM/ESD sources
- Linux images
- kernels
- SSH public keys
- offline packages

Features:

- filesystem detection
- read-only initial mount
- safe unmount/eject
- free-space validation
- large-file support
- hash verification

---

## 57. USB Mass Storage

Optional advanced feature.

Rules:

- explicit partition/image selection
- read-only default
- local mount must be released
- write lock/ownership state visible
- UDC identity validated
- stop/export before local remount

Do not export entire UFS LUN casually.

---

## 58. Disk/Partition Imaging

Functions:

- raw backup
- sparse-aware backup where valid
- compressed image
- chunked image
- image verification
- partial range capture for diagnostics
- restore with exact target identity

Image metadata must describe the original geometry.

---

## 59. File Recovery

Later capability:

- copy files from damaged but mountable filesystem
- read-only recovery destination selection
- preserve metadata when possible
- checksum copied files
- rescue selected directories to USB/host

Avoid embedding heavyweight forensic tooling until core features are stable.

---

## 60. Automatic Pre-Repair Snapshot

For a healthy writable Btrfs root:

```text
@uke-pre-repair-YYYYMMDD-HHMMSS
```

can be created before supported file-level Linux repairs.

Conditions:

- filesystem healthy enough
- enough space
- root subvolume identified
- snapshot operation verified
- user informed

Snapshots are not substitutes for external backups.

---

## 61. Configuration Diff

Compare:

- current vs snapshot
- current vs backup
- current vs package default where a reliable source is available

Especially useful for:

```text
/etc/fstab
/etc/crypttab
BLS entries
dracut config
systemd units
network configuration
```

Actions:

```text
Restore current file from snapshot
Open both
Edit current
Export diff
```

---

## 62. Search Across Mounted System

Useful rescue function:

- filename search
- text search with limits
- filter by filesystem/subvolume
- search config files
- locate kernel/initramfs
- find UUID references

Avoid unconstrained searches over enormous Windows/Linux trees by default.

---

## 63. Permission / SELinux Tools

For Linux rescue:

- inspect mode
- owner/group
- ACL
- xattrs
- SELinux context
- capabilities

Allow manual correction only in Advanced mode.

For Fedora, favor distro-native relabel mechanisms over inventing labels.

---

## 64. Time and Clock Diagnostics

Recovery can detect:

- RTC/system time
- grossly invalid clock
- filesystem timestamp anomalies
- TLS/SSH impact

Network time sync may be optional after networking is established.

Do not require Internet access for recovery.

---

## 65. Power/Battery Safety Gates

Before risky writes:

- battery level
- charging state
- external power presence where available
- temperature sanity

Potential policy:

```text
low battery + no charger → block repartition/large restore
```

Thresholds should be evidence-based and configurable for engineering builds.

---

## 66. Thermal Diagnostics

Display sensors by discovered type/name, not hard-coded thermal-zone numbers.

Functions:

- list zones
- temperature
- trend
- throttling-related logs
- battery temperature
- storage temperature if available

---

## 67. Long Operation Manager

Operations such as:

- WIM apply
- Btrfs scrub
- Btrfs send
- filesystem image backup
- large restore

need:

- progress
- transferred bytes
- speed
- elapsed time
- phase
- cancel semantics
- resume capability where safe
- log
- screen-off policy
- power safety

---

## 68. Operation Cancellation Rules

Not every operation is safely cancellable.

Each operation declares:

```text
cancel-safe
cancel-at-boundary
not-cancellable
```

UI must not show a generic Cancel button if cancellation can corrupt metadata.

---

## 69. Recovery Session State

Track:

- active mounts
- open crypto mappers
- chroots
- SSH clients
- mass-storage exports
- active long operations
- current transaction

Before reboot/poweroff:

```text
clean up session
sync
close safe mappings
unmount where required
warn about incomplete transaction
```

---

## 70. Multi-Installation Support

Do not assume exactly one Linux or Windows installation.

Example:

```text
Linux installations
1. Fedora Rawhide
2. Arch Linux ARM

Windows installations
1. Windows 11 ARM64
```

Boot button behavior:

- exactly one Linux → direct one-shot boot
- multiple Linux installs → choose entry
- remember default only if explicitly requested

---

## 71. OS Ownership Labels

Storage Graph can classify:

```text
ANDROID_FIRMWARE
ANDROID_SYSTEM
ANDROID_DATA
LINUX_ROOT
LINUX_BOOT
WINDOWS_ROOT
ESP_SHARED
RECOVERY
UNKNOWN
```

Unknown partitions must never be automatically reformatted.

---

## 72. Shared ESP Policy

If Android/Aloha/Linux/Windows share an ESP:

- never assume sole ownership
- preserve unknown EFI applications
- per-OS subdirectories
- backup before boot-file modification
- collision detection
- free-space check
- FAT filesystem health check

---

## 73. Android / Linux / Windows Separation

Avoid letting one OS repair feature mutate another OS implicitly.

Examples:

- Linux boot repair does not modify Android A/B slot metadata.
- Windows install does not wipe Fedora unless the explicit partition plan says so.
- Android OTA does not overwrite Uke ESP.
- GPT planner shows every affected OS.

---

## 74. Firmware Preservation

Even in “Fedora only” mode, do not interpret “only user OS” as permission to erase required Qualcomm/Xiaomi firmware partitions.

Preserve required:

- bootloader stages
- secure firmware
- modem/connectivity firmware where needed
- TEE
- calibration/persist-like data
- recovery route

---

## 75. Boot Chain Backups

Create a named set:

```text
boot-stack-<firmware>-<slot>/
├── boot.img
├── init_boot.img
├── vendor_boot.img
├── dtbo.img
├── vbmeta.img
├── recovery.img
├── manifest.json
└── SHA256SUMS
```

This is more useful than isolated unlabeled images.

---

## 76. AVB Inspector

Read-only features first:

- descriptors
- chain partitions
- algorithms
- public-key digest
- rollback index/location
- flags

Do not add a generic “Disable AVB” button as a core recovery feature.

---

## 77. Boot Image Inspector/Repacker

Potential features:

- parse header
- show header version
- kernel size/hash
- ramdisk size/hash
- command line
- bootconfig
- DTB
- vendor ramdisk fragments
- repack with explicit provenance

Repacking must preserve profile-specific structure and never imply a repacked image is trusted/signed.

---

## 78. Device Variant Detection

POCO Pad X1 and Xiaomi Pad 7 share `uke`, but release policy should distinguish:

- model
- SKU
- storage size
- region firmware
- panel/touch variant
- installed DTBO
- bootloader version

The UI can show a compatibility badge:

```text
PROFILE VERIFIED
PROFILE PARTIAL
UNKNOWN PROFILE
```

---

## 79. Firmware Profile Database

Versioned project data:

```text
profiles/
├── global-os3.0.303.json
├── china-os3.0.302.json
└── turkey-...
```

Fields:

- package hash
- partition layout hash
- expected boot-stack hashes
- known kernel identity
- known module ABI
- recovery constraints
- known hardware evidence

Profiles are evidence, not arbitrary allowlists.

---

## 80. Recovery Update Mechanism

Later feature:

- check local update package
- verify signature/hash
- verify model/profile
- preserve old recovery
- install one target only according to policy
- read back
- rollback path

Internet OTA for recovery is optional and should not precede signing/provenance work.

---

## 81. Plugin/Extension Model

Avoid arbitrary shell add-ons in stable recovery.

If extensibility is desired, use a manifest:

```json
{
  "id": "tool.example",
  "version": 1,
  "entrypoint": "...",
  "permissions": [
    "read-storage"
  ]
}
```

Potential permission classes:

```text
read-device
read-filesystem
network
write-files
write-filesystem
write-partition-table
write-boot
```

This is a later feature. Core functions should remain native/project-reviewed.

---

## 82. Security Model

Threats to consider:

- wrong block target
- malicious archive
- malformed GPT
- malformed WIM
- malformed XML/JSON
- malicious filesystem metadata
- command injection
- symlink attacks
- TOCTOU target replacement
- stale plans
- secret leakage
- SSH exposure
- recovery UI bypass
- malicious USB host
- malicious backup
- path traversal
- decompression bombs
- integer overflow in partition calculations

Use:

- bounded parsers
- no shell command construction from arbitrary strings
- `O_NOFOLLOW` where useful
- fd-based operations
- revalidation
- size limits
- explicit archive extraction policy
- fuzzing for parsers

---

## 83. SSH Security

Recommended defaults:

```text
off at boot
public-key authentication
temporary host/session policy considered
fingerprint shown locally
no blank password
no persistent authorized key without consent
rate limiting where feasible
session log metadata only, not command secrets
```

Optional high-security mode:

```text
SSH stops automatically when recovery screen is locked
```

---

## 84. ADB Security

Options:

- ADB off
- ADB on after unlock
- ADB on in engineering mode
- trusted-host mechanism if practical

At minimum, UI must clearly display when remote shell access is active.

---

## 85. Sensitive-Volume UI

When LUKS/BitLocker is unlocked:

```text
UNLOCKED
```

should be visually obvious.

Before enabling MTP/mass storage/SSH file sharing, clarify whether encrypted contents become remotely accessible.

---

## 86. Audit Log

Record operations without secrets.

Example:

```text
operation ID
time
action
target stable ID
plan hash
result
verification result
```

Do not log:

- passphrases
- recovery keys
- file contents
- personal directory listings unnecessarily

---

## 87. Reproducible Build / Provenance

Every release candidate should record:

- OrangeFox manifest revisions
- Uke repo commit
- toolchain
- host container/image if used
- package source revisions
- recovery kernel commit/config
- build identity policy
- SOURCE_DATE_EPOCH policy
- output hashes
- SBOM/license information
- privacy scan
- no-Python target payload scan per project policy

---

## 88. CI Test Classes

Use the workspace [test contract](https://github.com/MCC45TR/uke-linux/blob/codex/ure-roadmap-integration/docs/testing/TEST-CONTRACT.md)
and [PLAN.md section 8](https://github.com/MCC45TR/uke-linux/blob/codex/ure-roadmap-integration/PLAN.md#8-human-and-unattended-testing).
The recovery-specific cases below extend those classes without changing their
authorization or evidence boundaries. U0/U1/U2 are host/disposable/emulation
work; controlled device writes require H2.

### U0 — host static/unit

- parsers
- overflow
- path validation
- JSON
- GUID
- manifests
- transaction state machine

### U1 — disposable filesystem/GPT fixtures

- GPT corruption
- ext4
- Btrfs
- NTFS
- FAT
- LUKS
- synthetic BitLocker fixtures if lawful/practical
- power-loss simulation

### U2 — AArch64 userspace/QEMU

- binaries execute
- libraries resolve
- selected filesystem images
- recoveryctl API
- SSH service
- chroot helper logic

### U3 — previously accepted device fixture

- bounded read-only telemetry and non-destructive regressions only
- exact fixture/build/profile already accepted through H0/H1
- explicit stop conditions and an operator recovery route
- no unattended partitioning, formatting, slot mutation or firmware flashing

### H0 — hardware inventory/read-only

- device identity
- block layout
- bootloader
- slots
- return path

### H1 — recovery essentials

- boot
- display
- touch
- ADB
- USB
- read-only storage
- diagnostics

### H2 — controlled writes

- format test partition
- restore
- GPT operation
- snapshot rollback
- boot target switching
- recovery rollback

### H3 — stability

- repeated boots
- long transfers
- scrub
- network
- suspend/thermal if relevant

---

## 89. Fuzzing Targets

Good candidates:

- rawprogram XML
- GPT metadata parser
- profile JSON
- operation-plan JSON
- os-release parser
- BLS entry parser
- WIM metadata wrapper/parser boundaries
- diagnostic report import
- boot-request schema

Fuzzing is particularly valuable for any format read from removable media or arbitrary user files.

---

## 90. Negative Test Catalogue

Must test:

- duplicate PARTUUID/ambiguous fixture
- wrong label
- stale plan
- target resized after plan
- disk changed after plan
- source file changed after hash
- low storage
- full `/tmp`
- I/O error
- truncated read
- failed write
- partial write
- corrupt backup
- wrong-device backup
- mounted filesystem
- mapper already active
- snapshot merge active
- unknown firmware
- missing boot entry
- missing kernel
- wrong initramfs
- broken Btrfs snapshot
- wrong LUKS passphrase
- malformed BitLocker input
- disconnected SSH transfer
- power interruption boundaries

---

## 91. Recommended Implementation Roadmap

### Phase 0 — Preserve current safety baseline

- keep current fail-closed installer
- keep read-only Linux/ESP identity model
- finish physical recovery boot validation
- finish profile/device evidence

### Phase 1 — Core library refactor

- create `libuke-recovery`
- move inventory/identity logic into library
- structured result/error model
- JSON interface
- storage graph
- unit tests

### Phase 2 — Diagnostics foundation

- recovery stages
- pstore
- kernel/modules
- display
- touch
- USB
- storage
- report exporter

This phase is valuable before destructive features because it helps bring up the recovery itself.

### Phase 3 — Transaction engine

- plan schema
- stable IDs
- revalidation
- backup contracts
- journal
- verification
- rollback states

### Phase 4 — GPT and filesystem inspection

- GPT backup/compare
- filesystem probe
- ext4/FAT inspection
- F2FS inspection
- storage UI

### Phase 5 — Linux discovery/rescue

- distro detection
- kernel detection
- BLS/EFI detection
- file manager metadata
- GUI text editor
- chroot
- offline journal
- boot diagnosis

### Phase 6 — Recovery kernel capability expansion

- Btrfs
- dm-crypt
- required crypto
- NTFS path
- pstore if feasible

Validate each new driver on test images before internal storage.

### Phase 7 — Crypto

- LUKS unlock
- lock
- metadata
- header backup
- BitLocker unlock
- secret UI/security

### Phase 8 — Btrfs full manager

- subvolumes
- snapshots
- rollback
- scrub
- balance
- send/receive
- advanced rescue

### Phase 9 — Boot Target Manager

- detected OS targets
- one-shot schema
- direct Android reboot
- Aloha integration
- direct Linux reboot
- direct Windows reboot
- history/failure detection

### Phase 10 — Network rescue

- USB networking
- SSH
- SFTP/SCP
- host-assisted report/backup

Wi-Fi remains a separate later subphase.

### Phase 11 — Android advanced manager

- complete A/B validation
- Virtual A/B
- super
- OTA
- FBE only after trust-path validation

### Phase 12 — Windows rescue

- NTFS
- WIM/ESD
- Windows detection
- BitLocker integration
- ESP
- BCD inspection/backup
- boot diagnosis

### Phase 13 — Partition layout designer

Only after transaction engine, backups and restore tests are mature.

- Android + Fedora
- Android + Windows
- Android + Fedora + Windows
- Fedora-only user layout
- custom layout

### Phase 14 — Wi-Fi rescue

- WLAN driver/firmware
- network UI
- secure credential handling
- SSH over Wi-Fi

### Phase 15 — Release hardening

- security review
- fuzzing
- long operation tests
- privacy audit
- reproducible builds
- signed artifacts
- SBOM
- exact per-SKU acceptance records

---

## 92. Priority Matrix

### P0 — Required foundation

- physical recovery boot path
- stock return path
- device/firmware identity
- storage graph
- JSON API
- diagnostics
- transaction engine
- GPT backup
- boot-chain backup
- stale-plan rejection
- report exporter

### P1 — Core rescue value

- Linux detection
- Linux kernel detection
- GUI text editor
- Linux file manager
- ext4 rescue
- LUKS
- Btrfs
- chroot
- direct reboot targets
- USB networking
- SSH/SFTP
- Android advanced inspection

### P2 — Multi-OS management

- BitLocker
- NTFS
- WIM
- Windows rescue
- Aloha one-shot boot
- host-assisted large backup
- layout designer
- Wi-Fi

### P3 — Advanced / optional

- rich BCD editing
- plugin model
- advanced forensic file recovery
- remote UI
- broader distro-specific repair modules
- extra filesystems

---

## 93. Feature Acceptance Rule

Record each evidence dimension independently, with its scope and test record.
Use these labels without treating them as one automatic promotion ladder:

```text
PLANNED
SOURCE PRESENT
BUILT
HOST TESTED
EMULATION TESTED
DEVICE READ-ONLY TESTED
DEVICE WRITE TESTED
SUPPORTED
BLOCKED
```

Never collapse “binary exists” into “feature works”.

`BLOCKED` includes a concrete missing dependency or failed gate and may coexist
with a source/build result. `SUPPORTED` requires the complete feature contract
on the recorded physical SKU and firmware, with rollback and limitations.
Existing hardware-ledger result names remain unchanged; future implementation
states must link to that ledger rather than overwrite it.

Example:

```text
Btrfs
binary: built
kernel driver: missing
device support: BLOCKED
```

---

## 94. Suggested Status Page

The example below reflects the limited existing evidence described at the top
of this roadmap. It is not a generated hardware status page:

```text
UKE Recovery Capability Status

Recovery boot          DEVICE TEST REQUIRED
Display                DEVICE TEST REQUIRED
Touch                  DEVICE TEST REQUIRED
ADB                    DEVICE TEST REQUIRED

Linux/ESP identity      HOST TESTED (fixtures only)
ext4 read-only plan     HOST TESTED (live mount untested)
Linux distro/kernels    PLANNED
Btrfs                   BLOCKED: kernel driver
LUKS                    PLANNED
BitLocker               PLANNED

Android A/B             SOURCE PRESENT
                       installer policy host-tested; live HAL untested
Virtual A/B             BLOCKED / pending snapuserd path
Windows                  PLANNED
SSH                      PLANNED
```

---

## 95. Useful Additional Features Worth Considering

These are not all mandatory, but fit the project direction.

### 95.1 QR code for diagnostic session

Display:

- temporary SSH address
- host-key fingerprint
- report ID

Do **not** encode passwords.

### 95.2 Recovery Web UI

Much later, an optional local-only HTTP interface could show status/diagnostics to a laptop. This is lower priority than SSH and increases attack surface, so it should not be an early feature.

### 95.3 Serial/debug console awareness

If the platform exposes usable debug UART, provide documentation/status but do not assume retail hardware exposes it.

### 95.4 Screenshot/report annotations

Allow the tester to attach a short note to a diagnostic report.

### 95.5 Hardware test mode

Guided sequence:

```text
display
touch
buttons
USB
battery
charging
storage
network
audio later
```

Generate a result manifest.

### 95.6 Recovery self-test

On every engineering build:

- required binary presence
- library resolution
- config availability
- writable `/tmp`
- JSON schema sanity
- storage read-only probe
- kernel feature probe

### 95.7 Known-good profile snapshots

Record the last verified:

- Android boot stack
- Linux boot entry
- kernel/initramfs
- Windows ESP/BCD backup

Offer restoration without assuming every failure requires it.

### 95.8 Boot target health score — avoid

Do not reduce complex boot health to a misleading percentage. Show concrete checks and failed components instead.

---

## 96. Example End-to-End Linux Failure Workflow

Scenario: Fedora no longer boots after a kernel/update change.

```text
Boot recovery
   ↓
Systems → Linux
   ↓
Fedora Rawhide detected
   ↓
LUKS unlock
   ↓
Btrfs detected
   ↓
Diagnose Linux Boot
```

Result:

```text
Root @root                  OK
ESP                         OK
BLS default entry           kernel 7.2.9
Kernel image                OK
Initramfs                   MISSING
Modules                     OK
Previous kernel 7.2.8       COMPLETE
```

Actions:

```text
Boot previous kernel once
Rebuild 7.2.9 initramfs in chroot
Open BLS entry
Open terminal
Create rescue snapshot
```

This is substantially more useful than only providing a shell.

---

## 97. Example Btrfs Rollback Workflow

```text
Systems → Linux → Snapshots
```

Select:

```text
@root-before-update
```

Recovery:

1. verifies snapshot is readable
2. verifies kernel/boot relationship
3. snapshots current root as emergency fallback
4. creates one-shot Linux boot entry pointing to selected snapshot
5. syncs
6. reboots to Linux

Only after successful boot should a user optionally make the rollback permanent.

---

## 98. Example Windows Recovery Workflow

```text
Systems → Windows
```

Detected:

```text
Windows 11 ARM64
BitLocker locked
```

User supplies recovery passphrase.

Then:

```text
NTFS accessible
ESP detected
bootmgfw.efi present
BCD readable
```

Recovery options:

```text
Copy user files
Back up BCD
Back up Microsoft EFI tree
Inspect Windows boot
Apply WIM to a separate approved target
Repair preliminary NTFS state
Reboot to Windows once
```

---

## 99. Example Remote Rescue Workflow

```text
Network → USB Networking → Enable
Network → SSH → Enable
```

Screen shows:

```text
192.168.42.1
ED25519 fingerprint
```

On host:

```bash
ssh root@192.168.42.1
uke-recoveryctl linux info
uke-recoveryctl diagnose all --json
```

Large files/reports can be transferred over SFTP/SCP without relying on MTP.

---

## 100. Definition of the Long-Term Target

The final system should be able to answer, from recovery, questions such as:

```text
What exact tablet/SKU/firmware is this?
What partitions exist and who owns them?
Are GPT primary and backup tables valid?
Is an Android OTA merge in progress?
Which Android slot is active and healthy?
Which Linux distributions are installed?
Which kernel is Fedora configured to boot?
Does that kernel have a matching initramfs/modules/DTB?
Is Linux LUKS encrypted and can the user unlock it?
Which Btrfs subvolumes and snapshots exist?
Can the system boot a snapshot once?
Can a broken fstab/crypttab be edited safely?
Is Windows installed?
Is Windows BitLocker encrypted?
Are its EFI/BCD files readable?
Can a WIM be safely applied to an approved NTFS target?
Can the recovery boot Android/Linux/Windows once without an interactive boot menu?
Can logs be collected locally or over SSH?
Did the previous requested OS boot fail?
Can the user restore the machine to a known state?
```

If these can be answered reliably, UKE Recovery has moved beyond a device recovery port and become a real multi-OS maintenance platform.

---

## 101. Upstream / Technical References

These references are architectural inputs, not proof that a feature works on Uke hardware.

### OrangeFox

- OrangeFox GitLab: https://gitlab.com/OrangeFox
- OrangeFox Recovery tree: https://gitlab.com/OrangeFox/bootable/Recovery
- OrangeFox Wiki changelog: https://wiki.orangefox.tech/changelog

The Uke alpha pins `fox_16.0` recovery source at
`3d733672081bca3af42475a286145f4a8cdce4e7`, identifying R12.0, and locks
399 Android project revisions in the resolved manifest. The wiki's published
release labels are a separate input. Preserve exact revisions and the
[source audit](UKE-SOURCE-AUDIT.md), rather than selecting a floating
“latest version” name.

### Cryptsetup

- https://gitlab.com/cryptsetup/cryptsetup
- https://man7.org/linux/man-pages/man8/cryptsetup.8.html

Relevant capabilities include LUKS and existing BitLocker-compatible BITLK volume activation. BITLK activation supports password/recovery-passphrase/startup-key workflows, while arbitrary BitLocker header modification should not be part of the initial recovery design.

The upstream [BITLK documentation](https://gitlab.com/cryptsetup/cryptsetup/-/blob/main/man/cryptsetup.8.adoc)
describes those activation methods and unsupported header changes. Validate the
exact pinned target build and kernel crypto dependencies independently.

### Btrfs

- https://btrfs.readthedocs.io/

Relevant areas:

- subvolumes
- snapshots
- scrub
- balance
- send/receive
- rescue/check tooling

The upstream [check documentation](https://btrfs.readthedocs.io/en/latest/btrfs-check.html)
distinguishes default read-only checking from high-risk repair. The selected
Uke stock kernel still lacks Btrfs, regardless of userspace tool availability.

### wimlib

- https://wimlib.net/
- https://wimlib.net/man1/wimapply.html

On Unix-like systems, wimlib can apply a WIM directly to an unmounted NTFS block device using libntfs-3g while preserving Windows/NTFS metadata needed for real Windows deployment.

This is a conditional upstream capability from the [wimapply manual](https://wimlib.net/man1/wimapply.html),
not an existing Uke package or Windows deployment result. Pin the build with
NTFS support and verify fixture metadata plus isolated restore before use.

### systemd Boot Loader Interface / Boot Loader Specification ecosystem

- https://systemd.io/BOOT_LOADER_INTERFACE/
- https://systemd.io/BOOT/
- https://systemd.io/ROOTFS_DISCOVERY/

Useful design concepts include one-shot boot selection and versioned boot-entry identifiers. Uke/Aloha may implement its own compatible contract where direct use of these mechanisms is not possible.

The [Boot Loader Interface](https://systemd.io/BOOT_LOADER_INTERFACE/)
uses EFI variables and defines a consumed one-shot entry. Aloha support,
variable persistence and Android/recovery fallback still require their own
implementation and tests; recovery must not assume an EFI runtime is available.

---

## 102. Final Recommended Direction

Keep OrangeFox as the stable UI/recovery base, but make all Uke-specific intelligence live in project-owned native code.

The highest-value development order is:

```text
Recovery boots reliably
        ↓
libuke-recovery
        ↓
Storage Graph
        ↓
Diagnostics
        ↓
Transaction Engine
        ↓
Linux Discovery + Editor + Rescue
        ↓
Crypto
        ↓
Btrfs
        ↓
Direct OS Boot Targets
        ↓
USB Network + SSH
        ↓
Android Advanced Management
        ↓
Windows Rescue
        ↓
Full Multi-OS Partition Designer
```

This order gives the project useful debugging and rescue capabilities early, while postponing the most dangerous partition-layout changes until identity, backups, transactions and recovery paths are mature.

The final product should feel like a compact tablet-specific equivalent of:

```text
OrangeFox
+ rescue Linux
+ filesystem workstation
+ encryption manager
+ boot repair environment
+ multi-OS boot controller
+ hardware/recovery debugger
```

without losing the fail-closed behavior already present in the Uke repository.
