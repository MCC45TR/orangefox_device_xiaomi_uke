# OrangeFox R12.0 for Uke — experimental alpha

Unofficial development build for POCO Pad X1 / Xiaomi Pad 7 (`uke`).
**No physical-device tests. Global OS3.0.303.0.WOZMIXM only.**

- Separate temporary-boot IMG, dedicated recovery IMG and slot-safe installer ZIP.
- Native installer checks firmware hashes, slots and snapshot state; preserves
  inactive stock recovery and verifies the active-slot write.
- Bash/Toybox, ext4/FAT tools, GPT inspection, Linux/ESP read-only controls and
  rotation settings. No Python in the tablet payload.

Verify `SHA256SUMS`. Temporary boot: `fastboot boot OrangeFox-uke-fastboot-boot.img`;
never flash this asset. Install the ZIP from a working compatible recovery, or
follow the guarded active-slot flash procedure in `INSTALL.md`. Restore that same
slot with firmware-matched stock recovery if needed.

Read **[installation and rollback instructions](https://github.com/MCC45TR/orangefox_device_xiaomi_uke/blob/main/docs/PRE-RELEASE.md)**
before use. Decryption, Btrfs, flashlight, Linux/UEFI boot selection and seamless
updates are not validated or enabled as supported features. Images and ZIP are
unsigned; source pins, stock-kernel source and hashes accompany the release.
