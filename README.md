# OrangeFox Recovery for Xiaomi Pad 7 and POCO Pad X1

An independent OrangeFox recovery project for the `uke` device family (SM7675), with a tablet interface and native tools for storage planning, backups, Linux recovery and boot diagnostics.

[Releases](https://github.com/MCC45TR/orangefox_device_xiaomi_uke/releases) · [Installation guide](docs/INSTALLATION.md) · [Architecture](docs/ARCHITECTURE.md) · [Feature coverage](docs/FEATURE-PARITY.md) · [Uke Linux workspace](https://github.com/MCC45TR/uke-linux)

## Project status

**Experimental; neither commercial model has completed physical recovery acceptance.** The default branch, `R12.0`, follows the pinned official OrangeFox `fox_16.0` / Android 16 baseline. Xiaomi Pad 7 and POCO Pad X1 are tracked separately by model, SKU and installed firmware.

The feature summary below describes **current source**, including tools tested on disposable images and in generic virtual machines. Hardware test previews have separate build and package evidence. The owner confirmed touch navigation in a corrected live session; a replacement image still needs its own first-boot test.

Current source [blocks general live device writes](docs/RECOVERY-WRITE-POLICY.md) until the storage backend and selected device profile have been accepted. This covers Format Data, package installation, writable device mounts, fastbootd writes and the recovery installer. The separate [dualboot shell backend](docs/DUALBOOT-SETUP.md) implements narrowly checked F2FS userdata erase/recreation, but remains physically unaccepted and requires an already opened encryption map. Image transactions and read-only observations have separate validation paths. Advanced mode and confirmation cannot bypass general mutation gates; older release images retain their original behavior.

## Feature overview

The **Extra** menu groups the added tools by task, with matching icons and short descriptions. GUI actions and the `uke-recoveryctl` JSON CLI use the same native management library.

| Area | Current source capabilities | Details |
|---|---|---|
| Tablet interface | Preview and apply interface sizes from 50–100%; preset and custom selections; portrait/landscape layout; independently configured monitor scale, resolution and refresh rate; USB mouse and keyboard integration | [Display settings](docs/DISPLAY-SCALING.md) |
| Partition planning | Dualboot wizard with Linux/Windows checkboxes, optional Linux-only ESP, optional separate Linux boot, ext4/Btrfs/F2FS selection and proportional review; MB, GB, MiB, GiB or percentage input; combined filesystem and GPT image transactions with readback and rollback | [Dualboot setup](docs/DUALBOOT-SETUP.md), [partition manager](docs/PARTITION-MANAGER.md) |
| Stock-layout recovery | Coordinated GPT and selected stock-image restoration across six UFS LUN images, with pinned firmware inputs, protected ranges and interruption recovery | [Stock restoration](docs/STOCK-IMAGE-RESTORE.md) |
| Backups | Verified raw-image and chunked backups; host-assisted transfer over ADB; Linux and home directory backups preserving sparse files, hardlinks, extended attributes and ACLs; resumable capture and verified restore | [Linux/home backups](docs/TREE-BACKUP.md), [native API](docs/URE-NATIVE.md) |
| Filesystems and Btrfs | Staged format, check, repair and supported resize operations on filesystem images; native Btrfs subvolume, snapshot, full/incremental send, scrub, balance and rollback tools | [Filesystems](docs/FILESYSTEM-MANAGER.md), [Btrfs](docs/BTRFS-MANAGER.md) |
| Linux rescue and boot audit | Distribution detection; isolated Arch/Fedora-aware chroot; supported fstab and ESP mounting; resource-limited commands; kernel, initramfs, module, device-tree, UKI and BLS inspection | [Linux recovery](docs/LINUX-RESCUE-AND-BOOT.md) |
| Android and Windows inspection | Per-feature Android/boot admission reports; bounded read-only slot, snapshot and battery/thermal/UFS observations; Windows installation and EFI/BCD discovery; conditional WIM/ESD and NTFS inspection | [Platform admission](docs/PLATFORM-ADMISSION.md), [native commands](docs/URE-NATIVE.md) |
| Files and diagnostics | Metadata-aware browsing; a native text editor with fstab/crypttab/BLS validation; reviewed file replacement and rollback; bounded diagnostics and redacted report export | [Native tools](docs/URE-NATIVE.md) |

Availability depends on the selected filesystem, kernel, packaged tools and operation policy. A capability entry is not device acceptance. In particular:

- Partition transactions and six-LUN restoration have **regular-image** test
  evidence. The narrow live dualboot backend still requires physical acceptance
  and current-device preflight. New OS allocations use the original userdata
  range; encrypted userdata preservation/migration remains unavailable.
- Btrfs has separate generic ARM64 VM evidence. The preserved stock kernel
  lacks Btrfs support, and receive/restore remains unfinished.
- One-shot boot routing has a [fixture implementation](docs/BOOT-ROUTING.md);
  actual Uke/Aloha EFI execution, Android OTA workflows and encrypted-volume
  recovery still require further implementation or acceptance.
- Dock output modes, physical input and touch behavior need device tests.
  Automatic rotation and brightness currently report
  [sensor readiness](docs/SENSOR-READINESS.md), not working automatic controls.

## Downloads

The **v003 hardware test preview** adds a once-only screen synchronization
after the touch service starts, alongside repeated-tap and bottom-tab navigation
corrections. Use it only with the reviewed installed
kernel and keep a previously working recovery available.

| Download | Purpose |
|---|---|
| `orangefox-r12-uke-modded-v003.img` | Flash to the active recovery slot; not a `fastboot boot` image |
| `orangefox-r12-uke-modded-v003.zip` | Contains the same recovery image; ZIP installation is currently unavailable |

Download the two files from the
[test release](https://github.com/MCC45TR/orangefox_device_xiaomi_uke/releases/tag/R12.0-uke-modded-v003).
The IMG uses the tablet's installed boot stack. The current touch profile
requires kernel `6.1.175-android14-11-ga3b9c44908dd-ab13320413`; model name alone
does not establish compatibility. Battery reporting, Android decryption,
USB-hub mouse support and cold-start touch still require device acceptance.
Preferences survived a controlled GUI restart; full reboot and replacement-image
persistence remain untested. Follow the [installation and rollback guide](docs/INSTALLATION.md)
and [touch test boundary](docs/TOUCH-STARTUP.md).

## Development and validation

Device integration lives in `src/device/xiaomi/uke/`; native management code is under `recoveryctl/`. Reviewed upstream changes are maintained in `patches/`. Source archives under `referances/` are evidence inputs, not development trees. New device management code uses C++; tablet payloads contain no Python runtime.

Start with the [contribution rules](AGENTS.md), [source audit](docs/UKE-SOURCE-AUDIT.md) and [host build policy](docs/HOST-BUILD-BUDGET.md). The [native build report](reports/URE-NATIVE-BUILD.md), [write-gate tests](docs/WRITE-GATE-VM-TESTS.md) and [optimization audit](reports/URE-OPTIMIZATION-SECURITY-AUDIT-2026-10-04.md) record validation scope and remaining work. Source checks, host tests, ARM64 builds, extracted-payload tests, VM results and physical tablet results remain distinct evidence classes.

For a useful [issue report](https://github.com/MCC45TR/orangefox_device_xiaomi_uke/issues), include the source revision or artifact checksum, model, firmware, reproduction steps and observed result. Share a redacted report; keep unit identifiers, calibration data and private logs out of public issues.

## License and attribution

Repository-level material uses the [MIT license](LICENSE). The native recovery library includes an [Apache 2.0 license](src/device/xiaomi/uke/recoveryctl/LICENSE-APACHE); upstream OrangeFox, Android and other imported components retain their own licenses and attribution. Icon licensing is recorded with the [interface assets](src/device/xiaomi/uke/ui-icons/LICENSE).

This project is unofficial and is not an endorsed OrangeFox release.

## Software FMEA

See [the software FMEA](FMEA.md) for storage, interruption, crypto, boot routing,
input and diagnostic-export risks, existing controls and evidence required for
closure. The assessment keeps packed-image and physical acceptance separate.
