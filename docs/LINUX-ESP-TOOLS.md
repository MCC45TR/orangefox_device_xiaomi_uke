# Linux and ESP tools in Uke recovery

**18 October 2026 source status:** the existing `linux` and `esp` partitions are
registered as optional storage in recovery. In **Mount**, select **Linux** or
**ESP**, then use **Files** or the storage selector to browse `/linux` or `/esp`.
Linux uses ext4 without journal replay; ESP uses FAT32. Both mounts are read-only,
`nosuid`, `nodev` and `noexec`. They are excluded from formatting, wiping, factory
reset and settings storage. An absent partition is normal on a stock layout.
The new image and physical file-browser workflow still require device testing.

This does not recreate or format the accepted volumes. The dated ESP/Linux
creation and filesystem checks belong to
[REC-FB144](https://github.com/MCC45TR/uke-linux-docs/blob/main/docs/lessons/2026-10-11-RECOVERY-ESP-LINUX-CREATION.md).
Android files use the separate [read-only decryption path](ANDROID-FILE-ACCESS.md).

The existing project-owned `uke-recoveryctl` also retains explicit PARTUUID-bound
mount commands. It accepts the current `esp`/`linux` GPT names and historical
`uke_esp`/`uke_linux` names without renaming them. These commands use
`/mnt/uke-esp` and `/mnt/uke-linux`; the recovery storage entries above use
`/esp` and `/linux`. It never guesses a UFS LUN or partition number.

From the recovery terminal or ADB shell:

```sh
uke-recoveryctl list
uke-recoveryctl plan-mount esp PARTUUID
uke-recoveryctl plan-mount linux PARTUUID
uke-recoveryctl plan-mount linux-btrfs PARTUUID
uke-recoveryctl mount-ro esp PARTUUID
uke-recoveryctl mount-ro linux PARTUUID
uke-recoveryctl mount-ro linux-btrfs PARTUUID
uke-recoveryctl unmount esp PARTUUID
uke-recoveryctl unmount linux PARTUUID
```

Mounts are explicitly read-only, `nosuid`, `nodev` and `noexec`; ext4 uses
`noload` to avoid journal replay and Btrfs uses `rescue=nologreplay`.
The verified Global stock kernel configuration has `CONFIG_BTRFS_FS` disabled.
Therefore, Btrfs mounts cannot work with this stock-kernel recovery profile;
the Btrfs selector is a fail-closed plan for a future compatible kernel, not
an advertised working feature. Btrfs administration and formatting utilities
are not yet packaged. The upstream source is pinned separately in
[`btrfs-progs.lock.json`](../manifests/btrfs-progs.lock.json) for future porting.
These mount commands cannot create, resize, format or write a partition.
Separate native transaction commands retain their reviewed plan and write
policy. Bundled upstream tools are not a substitute for those controls.

The default display orientation remains 270 degrees. `uke-recoveryctl rotation
show` and `uke-recoveryctl rotation 0|90|180|270` read or set OrangeFox's
`persist.twrp.rotation` property. OrangeFox reads this property during graphics
initialization, so the change requires a recovery UI restart. Persistence across
recovery boots, rendering and touch-coordinate behavior must be verified on a
device before claiming four-orientation support. No destructive "rotation
data" wipe is used.

The previously built OrangeFox root contains Bash and a broad Toybox command set,
including `sh`, `awk`, `sed`, `grep`, `find`, `tar`, `dd`, `mount`, `umount`,
`chroot`, `sha256sum` and `vi`. The project does not bundle an unknown BusyBox
prebuilt. OrangeFox's generic flashlight UI is disabled in this profile:
there is no verified Uke torch LED path, and Xiaomi's model specifications
do not establish a rear-camera flash. A hardware-matched path and power/current
test are required before enabling it.
