# OrangeFox Recovery for POCO Pad X1 and Xiaomi Pad 7

A device-specific OrangeFox recovery for POCO Pad X1 and Xiaomi Pad 7 (`uke`, SM7675). The project builds on the official OrangeFox Android 16 source branch and develops recovery controls for diagnosis, backup, installation and future Fedora boot management. Model, SKU and firmware compatibility remain explicit release dimensions.

[Uke Linux](https://github.com/MCC45TR/uke-linux) · [Recovery architecture](docs/ARCHITECTURE.md) · [Feature coverage](docs/FEATURE-PARITY.md) · [Source audit](docs/UKE-SOURCE-AUDIT.md) · [Releases](https://github.com/MCC45TR/orangefox_device_xiaomi_uke/releases)

Release candidates use [separate fastboot-boot, recovery-flash and ZIP assets](docs/PRE-RELEASE.md); none is interchangeable with another. [A/B, OTA, boot-selection and encryption support](docs/RECOVERY-VERSION-GATES.md) is tracked by device and firmware, not by configuration flags alone.

## Planned capabilities

- OrangeFox installation, backup/restore, ADB, sideload, MTP and fastbootd with verified target selection.
- A device status and diagnostics interface with redacted log export.
- Storage and boot-profile planning for Android with Fedora or Fedora as the single user OS.
- Screen rotation, touch, brightness, language and battery integration adapted to Uke.
- Conditional advanced tools for Linux partitions, ESP, OTA payloads and additional operating systems, enabled only when their dependencies are proven.

The [34-group feature matrix](docs/FEATURE-PARITY.md) captures the minimum functionality requested from the Nabu reference. Its partition offsets, GPT backups, security binaries and kernel images are not Uke inputs.

## Downloads

See [GitHub pre-releases](https://github.com/MCC45TR/orangefox_device_xiaomi_uke/releases) for experimental build candidates and [installation/rollback instructions](docs/PRE-RELEASE.md). The first alpha supplies separate temporary-boot IMG, dedicated recovery IMG and active-slot installer ZIP, plus hashes and source snapshots. **Neither commercial model has been boot-tested. Global OS3.0.303.0.WOZMIXM is the only packaged profile.** These unsigned development artifacts are not a supported recovery; do not use them on another firmware or treat a source/host check as a hardware result.

The current source baseline is official OrangeFox `fox_16.0`, which identifies its release series as R12.0. The branch revision is pinned in the workspace archive catalog and is updated through reviewed source changes. The Uke-specific device configuration is under development; existing community trees are reference material.

## For developers

The workspace [PLAN.md](https://github.com/MCC45TR/uke-linux/blob/main/PLAN.md) sets the implementation order and evidence gates. This repository owns `src/`, `configs/`, `patches/`, `tests/`, documentation and local `referances/`. New device management code targets C++; tablet payloads contain no Python. See [AGENTS.md](AGENTS.md) before contributing. Imported OrangeFox and donor code retains its original license and attribution.

This is an independent, unofficial device project and is not an official OrangeFox release.
