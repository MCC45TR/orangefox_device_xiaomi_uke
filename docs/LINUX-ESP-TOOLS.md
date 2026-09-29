# Linux and ESP tools in Uke recovery

The local Uke recovery build includes the project-owned `uke-recoveryctl`
command and the upstream `sgdisk`, `e2fsck`, `mke2fs`, `resize2fs`, `tune2fs`,
`fsck.fat` and `mkfs.fat` utilities. Tool presence does **not** authorize a
storage operation. No Linux or ESP partition is declared in `recovery.fstab`,
because neither partition exists in the measured stock Global layout.

The first Uke partition names reserved for a future, verified Linux layout are
`uke_esp` (FAT, mounted at `/mnt/uke-esp`) and `uke_linux` (ext4 or Btrfs,
mounted at `/mnt/uke-linux`). The control accepts only a full GPT PARTUUID matching the
requested exact name. It never guesses a UFS LUN or a partition number.

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
The command cannot create, resize, format or
write a partition. Direct invocations of the bundled upstream tools are not
guarded by this command and must wait for an approved Uke-specific storage plan,
verified backup/return path and physical-device acceptance. A future setup UI
must use the same identity checks before any write. The recovery is not yet
boot-tested on either commercial model.

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
