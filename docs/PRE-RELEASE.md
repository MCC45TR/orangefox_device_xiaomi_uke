# Uke recovery pre-release gate

This is the short release format for a future hardware-validation pre-release.
Do not publish an installable release until every artifact is independently
built and its exact use is validated on the matching device and firmware.

## Asset roles

| Asset | Intended command after validation | Current status |
|---|---|---|
| `OrangeFox-uke-fastboot-boot.img` | `fastboot boot OrangeFox-uke-fastboot-boot.img` | Not built or boot-tested; a recovery image with no embedded kernel is not this asset |
| `OrangeFox-uke-recovery.img` | Flash the confirmed recovery slot only after device-specific rollback checks | Local source build only; privacy, AVB and hardware gates open |
| `OrangeFox-uke-flashable.zip` | Install from a compatible recovery after installer target audit | Local source build only; installer and signing gates open |

Do not rename or duplicate one image to fill another role. The current local
`recovery.img` has a zero-byte embedded kernel and is not advertised for
`fastboot boot`. Do not run a generic `fastboot flash recovery` command on an
unidentified slot or locked bootloader. The exact install/rollback commands
belong in the release only after both commercial models and their firmware
profiles are checked.

The published instructions will distinguish temporary boot (`fastboot boot
OrangeFox-uke-fastboot-boot.img`), flashing only the confirmed
`recovery_<active-slot>` partition with `OrangeFox-uke-recovery.img`, and
installing `OrangeFox-uke-flashable.zip` from an already working recovery.
Before any flash, check model, firmware, unlocked state, active slot, snapshot
merge status, partition size and SHA-256. Keep the firmware-matched stock
`recovery.img` and a validated return command for that same slot. These are
future instructions, **not commands approved for the current local artifacts**.

## Proposed concise release text

> **OrangeFox for POCO Pad X1 / Xiaomi Pad 7 — pre-release**
>
> Early `uke` hardware-validation build. Assets are model/firmware-specific;
> verify the device, unlocked bootloader, active slot and SHA-256 before use.
> See the per-asset installation and rollback instructions below. Linux/ESP
> discovery is read-only. Btrfs, rotation/touch and flashlight remain subject
> to device validation. Do not use on an unsupported firmware or SKU.

Every published release must supply asset hashes, exact source revision,
compatible firmware, what was actually boot-tested, flashing instructions and
a verified stock return route. The existing private local images fail the
payload privacy audit and have an AVB `NONE` footer; they are not release assets.
