# Shared native recovery write policy

The stock OrangeFox Format Data route now consults the shared native policy
before looking up userdata, unmounting metadata, inspecting snapshot state or
running a formatter. A confirmation page, an advanced-mode selection, a writable
fstab entry or an Android property cannot authorize this route.

The accepted live URE storage backend is still unavailable. Consequently these
legacy device mutation paths are refused, rather than presented as validated
live operations. Reviewed URE transactions on regular image files remain a
separate supported host/VM capability.

## Native coverage

- Wipe, factory reset and Format Data; filesystem formatters, discard, repair
  and resize; raw/tar/sparse restore and image flashing.
- Package/OTA installation and sideload, including alternate AOSP entry points.
  Sideload refuses before disconnecting the normal ADB service.
- Boot-slot switching, AVB/verity patches, recovery injection and post-install
  scripts, including unsuccessful installation cleanup paths.
- Writable mounts and readonly-to-writable transitions; credential/decryption
  entry points; USB mass-storage exposure and the writable MTP service.
- Managed file commands, SELinux context changes and arbitrary ORS commands.
- Fastbootd erase/flash, slot and logical-partition mutation, super/GSI/snapshot
  updates, OEM commands and any command absent from the explicit allowlist.
  Direct mutating handlers and writable partition opens are guarded as well.
- The packaged recovery installer refuses `install` before opening its input
  image, reading credentials, starting bootctl, preparing a backup or opening a
  device. Its `check` mode retains the existing read-only firmware checks.

The fixed census in `configs/ure/legacy-write-entry-points.tsv` requires an
early guard at each enumerated native entry. Complete production Format Data,
slot, mount, fastboot dispatch and block-open functions are compiled with fake
hardware callbacks. Their tests independently verify unchanged fixture bytes.
The production installer is tested with intercepted open/fork/ioctl calls. A
deliberately removed Format Data guard must fail both the census and the
compiled behavioral fixture. This is a regression gate, not proof that a new
upstream mutation API can be added without review.

## Read and recovery behavior

Initial slot observation remains available without a boot-control HAL write.
Read-only inspection and unmount/namespace cleanup are retained. Fastbootd
permits `getvar`, RAM-only `download`, `fetch` and explicit reboot/shutdown
controls; reboot routes may update their normal boot-control message.

Readonly ext4 mounts retain `noload`, including fallback attempts. F2FS retains
`norecovery`. An existing writable or uninspectable mount is refused instead of
silently accepted or remounted. NTFS/exFAT helpers receive `ro`, and the exFAT
kernel-to-FAT fallback retains `MS_RDONLY`. The device fstab also requests
readonly, no-replay mounts for data, metadata and persist. Linux documents that
ext4 can replay its journal even under `ro`; skipping replay can expose an
inconsistent view after an unclean shutdown. See the [ext4 mount contract](https://docs.kernel.org/admin-guide/ext4.html)
and [F2FS mount options](https://docs.kernel.org/filesystems/f2fs.html).

The fastbootd distinction follows the [AOSP userspace fastboot command contract](https://source.android.com/docs/core/architecture/bootloader/fastbootd).
Command transfers and inspection do not grant permission to flash or erase.

## Boundary and future live acceptance

This policy controls project-managed native recovery operations. It is not a
sandbox against an intentional root ADB/terminal command, a separately launched
raw writer, the bootloader's own fastboot implementation or an OEM HAL. Normal
ADB inspection remains available. Read-side hardware behavior and installed
firmware compatibility still require their own physical acceptance records.

The future live backend must bind an exact device/firmware profile and storage
ownership to a reviewed immutable plan, verified backup, durable journal,
fresh slot/mount/holder/snapshot checks, controlled mutation and readback. It
must preserve the firmware-matched recovery route and prove forced-reboot
recovery on each accepted device/profile. It must not enable legacy entry
points through a property or a global advanced-mode switch. Protected
partition changes require their own target-bound reviewed plan.

Previously sealed images remain unchanged. Rebuilding source and testing a new
image cannot transfer that protection to an older candidate, and no tablet
format/flash/restore has been performed for this change.
