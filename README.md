# OrangeFox Recovery for POCO Pad X1 and Xiaomi Pad 7

A device-specific OrangeFox recovery for POCO Pad X1 and Xiaomi Pad 7 (`uke`, SM7675). The project builds on the official OrangeFox Android 16 source branch and develops recovery controls for diagnosis, backup, installation and future Fedora boot management. Model, SKU and firmware compatibility remain explicit release dimensions.

[Uke Linux](https://github.com/MCC45TR/uke-linux) · [Recovery architecture](docs/ARCHITECTURE.md) · [Feature coverage](docs/FEATURE-PARITY.md) · [Source audit](docs/UKE-SOURCE-AUDIT.md) · [Releases](https://github.com/MCC45TR/orangefox_device_xiaomi_uke/releases)

Release candidates use [separate fastboot-boot, recovery-flash and ZIP assets](docs/PRE-RELEASE.md); none is interchangeable with another. [A/B, OTA, boot-selection and encryption support](docs/RECOVERY-VERSION-GATES.md) is tracked by device and firmware, not by configuration flags alone.

Current-source builds [refuse legacy device mutations](docs/RECOVERY-WRITE-POLICY.md)
at their native entry points, including stock Format Data, package installation,
writable mounts, fastbootd writes and the installer. Confirmation or advanced
mode cannot accept the unfinished live backend. Earlier sealed images are
unchanged; the [write-refusal tests](docs/WRITE-GATE-VM-TESTS.md) describe the
separate host and generic-guest validation.

## Planned capabilities

- OrangeFox installation, backup/restore, ADB, sideload, MTP and fastbootd with verified target selection.
- A device status and diagnostics interface with redacted log export.
- Storage and boot-profile planning for Android with Fedora or Fedora as the single user OS.
- Screen rotation, touch, brightness, language and battery integration adapted to Uke.
- Conditional advanced tools for Linux partitions, ESP, OTA payloads and additional operating systems, enabled only when their dependencies are proven.
- Linux distribution/kernel discovery, metadata-aware file management and a native GUI text editor, controlled chroot and offline boot diagnosis.
- LUKS/BitLocker access, Btrfs subvolumes/snapshots/rescue, NTFS/WIM and Windows ESP/BCD recovery behind kernel and transaction gates.
- Consumed one-shot Android/Linux/Windows boot requests, USB networking and opt-in key-authenticated SSH/SFTP, followed by Wi-Fi rescue.

The [feature matrix](docs/FEATURE-PARITY.md) retains the 34-group Nabu minimum
and adds 24 URE capability contracts. The [comprehensive roadmap](docs/COMPREHENSIVE-ROADMAP.md)
preserves all supplied topics 0–102, with sixteen phases, proposed API/UI designs,
transactions, negative tests and acceptance rules. The [native implementation
checkpoint](docs/URE-NATIVE.md) records the new library, JSON CLI, file transaction
engine, diagnostics and OrangeFox editor adapter. The published first alpha
predates this work. The [native build report](reports/URE-NATIVE-BUILD.md)
separates source, host, extracted-payload, QEMU and hardware evidence. Most roadmap
contracts remain incomplete. The [streaming and journal checkpoint](reports/URE-STREAMING-BUILD.md)
adds verified chunk transfer, host reception and explicit interrupted-file recovery.
The [GPT checkpoint](reports/URE-GPT-BUILD.md) adds metadata backup/verification,
image repair/restore, inspected journals and read-only live selection. Real UFS
writes and full phase acceptance remain open.
The [six-LUN stock checkpoint](reports/URE-STOCK-JOB-BUILD.md) coordinates stock
GPTs and selected hash-pinned Global OS images under one inspected image journal.
Its [CLI and GUI contract](docs/STOCK-IMAGE-RESTORE.md) covers sparse decoding,
capacity-derived layouts, protected firmware/tails and complete changed-range
rollback. Model/SKU declarations do not establish either tablet's acceptance.
The [stock preflight correction](reports/URE-STOCK-PREFLIGHT-BUILD.md) separates
source-file checksums from complete programming-layout checksums. DTBO's
reviewed larger layout includes its zero gap and duplicated AVB end footer;
[verification](docs/STOCK-BOOT-PREFLIGHT.md) does not rewrite installed firmware.
The [boot-routing contract](docs/BOOT-ROUTING.md) adds registered EFI inventory,
target/default review and one-shot private-fixture journals with consumed
attempts, retired-plan replay refusal and inspected fallback/history. Real
firmware writes, EFI execution and Uke/Aloha routing remain unaccepted.
The [storage checkpoint](reports/URE-STORAGE-BUILD.md) adds bounded ownership
observations and identity-bound storage backup software with shared CLI/GUI/host
reception. The raw-image restore engine adds verified original/target mirrors,
durable journals, full readback and inspected interruption recovery. Positive
live-source acceptance, atomic snapshots and real block restores remain open.
Nabu partition offsets,
GPT backups, security binaries and kernel images are not Uke inputs.

The local [filesystem and Linux rescue checkpoint](reports/URE-RESCUE-FILESYSTEMS-BUILD.md)
adds staged filesystem-image jobs, distribution-aware isolated chroot, installed
kernel/initramfs/DT/UKI/BLS audit and native Btrfs snapshot/send/maintenance pages.
Btrfs has separate generic ARM64 VM evidence; the preserved stock kernel still
has no Btrfs filesystem support. Live writes and both tablets' physical acceptance
remain open. This candidate has not been published to GitHub.

## Downloads

See [GitHub pre-releases](https://github.com/MCC45TR/orangefox_device_xiaomi_uke/releases) for experimental build candidates and [installation/rollback instructions](docs/PRE-RELEASE.md). The first alpha supplies separate temporary-boot IMG, dedicated recovery IMG and active-slot installer ZIP, plus hashes and source snapshots. **Neither commercial model has been boot-tested. Global OS3.0.303.0.WOZMIXM is the only packaged profile.** These unsigned development artifacts are not a supported recovery; do not use them on another firmware or treat a source/host check as a hardware result.

The current source baseline is official OrangeFox `fox_16.0`, which identifies its release series as R12.0. The branch revision is pinned in the workspace archive catalog and is updated through reviewed source changes. The Uke-specific device configuration is under development; existing community trees are reference material.

## For developers

The workspace [PLAN.md](https://github.com/MCC45TR/uke-linux/blob/main/PLAN.md) sets the implementation order and evidence gates. This repository owns `src/`, `configs/`, `patches/`, `tests/`, documentation and local `referances/`. New device management code targets C++; tablet payloads contain no Python. See [AGENTS.md](AGENTS.md) before contributing. Imported OrangeFox and donor code retains its original license and attribution.

This is an independent, unofficial device project and is not an official OrangeFox release.
