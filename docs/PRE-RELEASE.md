# Experimental Uke recovery

**Publication status — 6 October 2026:** the earlier alpha release has been
withdrawn. No replacement installable release is currently published. Commands
below document artifact roles and the historical evaluation procedure; they do
not authorize installing a new candidate whose build or installer checks are
incomplete. The current source refuses sideload installation before writing.
The first owner-operated image experiment has a narrower
[first-device boot scope](FIRST-DEVICE-BOOT.md), including input, charging,
backlight and bootloader-return limitations.

Unofficial OrangeFox R12.0 / Android 16 for the `uke` device family: POCO Pad X1
and Xiaomi Pad 7. **Neither model has been boot-tested.** These are development
artifacts, not a supported recovery release. Keep a complete backup and the
firmware-matched stock recovery on your host. An unlocked bootloader is required;
this release does not unlock it, bypass AVB or change trusted firmware.

The only packaged firmware profile is **Global OS3.0.303.0.WOZMIXM**. CN, Turkey,
other HyperOS versions and Android 14/15/17 are not installation targets. The
stock GKI kernel is used without modification; source identification and its
exact snapshot accompany the release. Binary reproduction has not been performed.

## Choose the correct file

| Asset | Purpose | Important limitation |
|---|---|---|
| `OrangeFox-uke-fastboot-boot.img` | Temporary `fastboot boot` experiment; contains stock GKI kernel and recovery ramdisk | Never flash this file. Bootloader acceptance and vendor-ramdisk handoff are untested. |
| `OrangeFox-uke-recovery.img` | Dedicated 100 MiB recovery image; kernel supplied by the stock boot chain | Write only the verified active recovery slot, never both. Not a `fastboot boot` image. |
| `OrangeFox-uke-flashable.zip` | Install from an already working Android recovery | Native checks must succeed; unavailable boot-control/snapshot HAL causes refusal. Unsigned development ZIP. |

Verify `SHA256SUMS` before using any asset:

```sh
sha256sum --check --ignore-missing SHA256SUMS
```

## Temporary boot

Only on the matching stock boot/vendor_boot/dtbo stack, with the bootloader
unlocked and no OTA/snapshot operation in progress:

```sh
fastboot getvar product
fastboot getvar current-slot
fastboot boot OrangeFox-uke-fastboot-boot.img
```

Product must be `uke`. If boot is refused, hangs, or USB/display/touch fails,
return to the stock bootloader; do not flash the temporary image as a workaround.
This file has an unsigned AVB `NONE` footer, not an OEM signature. A successful
host package check says nothing about whether an Uke bootloader will accept it.

## ZIP installation and preflight

The current source disables ZIP/device installation until the live URE backend
has its own acceptance record. `uke-recovery-install install` refuses before
opening an image or a device. The ZIP invokes that mode and therefore cannot
install this checkpoint. Older sealed packages retain their original behavior;
their instructions are historical, not acceptance for the new source.

The read-only `check` mode uses a working recovery with an operational
boot-control HAL. The ZIP includes a
static AArch64 C++ helper, not the upstream dual-slot installer. It requires
`uke`, unlocked-state evidence, exactly two consistent slots, snapshot status
`none`, a bootable inactive slot, exact stock303 hashes for boot, init_boot,
vendor_boot and dtbo on **both slots**, and stock recovery on the inactive slot. It
checks real block-device labels and sizes. These checks do not authorize a
write through the disabled installer. It does not format/mount userdata,
switch slots, touch vbmeta or reboot automatically.

To run the same checks without writing, extract the helper and send it and the
recovery image to `/tmp` in the working recovery:

```sh
unzip -p OrangeFox-uke-flashable.zip uke-recovery-install > uke-recovery-install
adb push uke-recovery-install /tmp/uke-recovery-install
adb push OrangeFox-uke-recovery.img /tmp/OrangeFox-uke-recovery.img
adb shell chmod 700 /tmp/uke-recovery-install
```

Run `/tmp/uke-recovery-install check /tmp/OrangeFox-uke-recovery.img SHA256`
through `adb shell`, replacing `SHA256` with the image hash in `SHA256SUMS`.
Missing evidence is a rejection, not permission to bypass checks. No backup
or installation is produced by the disabled `install` mode. Read
[the shared native write policy](RECOVERY-WRITE-POLICY.md) before considering
the separate, unaccepted bootloader experiment below.

An older/different inactive firmware causes refusal too. Do not update, clone
or switch that slot just to satisfy this experimental installer.

The current source also distinguishes OEM image lengths from full partition
programming checksums. Its reviewed DTBO check includes the source bytes, zero
gap and AVB footer duplicated at the 24 MiB partition end; it never pads or
rewrites an existing DTBO to pass. This is a source-derived AOSP layout, not an
accepted physical dump. A different installed tail policy remains refused;
see [the boot preflight contract](STOCK-BOOT-PREFLIGHT.md).

## Manual recovery flash and stock return

First pass the native preflight above, save current recovery, and record the
verified active slot. Reboot to the bootloader, confirm it still reports that
slot and that its recovery partition is `0x6400000` bytes. For verified active
slot **a**, the commands are:

```sh
fastboot getvar current-slot
fastboot getvar partition-size:recovery_a
fastboot flash recovery_a OrangeFox-uke-recovery.img
fastboot reboot recovery
```

For verified active slot **b**, use `recovery_b` instead. Never issue a bare
`fastboot flash recovery` or flash both slots. To restore, return to fastboot
and flash the **same recorded slot** with `stock303-recovery.img` extracted
from the verified stock303 fastboot package; expected SHA-256:
`a22c93ccd0d439d610547a47ab4d8001f72ee769f791991f65d47d5724db049b`.
For recorded slot a: `fastboot flash recovery_a stock303-recovery.img`, then
`fastboot reboot recovery`. Do not change slots or erase metadata/userdata as
a recovery workaround. This is a package-derived return procedure, **not a
physically rehearsed rollback**.

## Included and unavailable

Included: upstream recovery tools, ADB/sideload/fastbootd configuration, Bash,
Toybox, ext4/FAT utilities, GPT inspection, native Linux/ESP read-only controls
and rotation-property controls. Runtime USB, display, touch, rotation, backup
and OTA compatibility still require device tests. Shared recovery writer
boundaries now guard managed formatting, flashing, restore, OTA, fastbootd and
misc/BCB mutation. These managed operations remain unavailable until live
storage acceptance; an authorized root ADB shell can issue raw writes outside
that API policy.

Fonts use the reviewed, license-identified font sources and packaged aliases;
font names are not evidence of complete glyph or physical display acceptance.
Generic FRP, AVB-disable
and verity/encryption-edit addon recipes are omitted. Bundled generic addon
ZIPs are omitted as well: an embedded legacy updater failed the privacy scan.
Their optional UI actions are not supported in this alpha; use the native
project installer and commands documented here.

Unavailable: Android data decryption, working Btrfs mounts (stock kernel lacks
Btrfs), flashlight, Fedora/UEFI boot selection, repartitioning and a validated
seamless-update path. No Android 14–17 decryption promise is made. No stock
vendor HAL blobs, stock vendor_boot or proprietary firmware are redistributed.
