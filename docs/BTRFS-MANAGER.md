# Native Btrfs manager

The Btrfs page and CLI call Linux UAPI directly. They do not launch a btrfs-progs
process. Select an already mounted Btrfs filesystem with a matching recovery
kernel. The preserved Global stock kernel has Btrfs disabled; shipping a
userspace manager does not enable it. Generic 7.2.8 VM modules are test inputs
and must never be loaded into the stock 6.1 recovery kernel.

| Operation | Review and scope |
| --- | --- |
| info, subvolumes, usage, device-stats, scrub-status, balance-status | bounded read-only native inventory, UUID/lineage, allocation groups and counters |
| create, snapshot | exact unused destination; same-filesystem validation; optional read-only snapshot |
| rename | unmounted child renamed within its existing parent; unused destination, exact source UUID/inode and parent checks; no overwrite |
| readonly | explicit flag; received snapshots cannot be made writable and lose their incremental lineage |
| delete | exact child subvolume with a retained derived read-only backup; nested subvolumes are not forced away |
| rollback | derived read-only snapshot cloned writable, atomic path exchange, original retained at an unused saved path |
| resize | one-device filesystem; 4 KiB sizes of at least 256 MiB; kernel validates allocation; underlying partition is not resized |
| scrub | exact device ID and explicit repair choice; verification-only requires a read-only mount |
| balance | usage filter at most 90 percent and limit of 1–128 chunks; no unbounded full balance |
| scrub-cancel, balance-pause, balance-cancel | explicit reviewed control of the selected filesystem's maintenance job |

Plans seal the selected filesystem, mount, subvolume identity/flags/generation,
request and firmware-profile identifier. Apply revalidates them. The top-level
root cannot be deleted/replaced/renamed or have its flags changed. Rollback/delete/rename
reject mounts in visible namespaces, open files, process working directories
and process roots inside the affected subvolume. Private journals must remain
outside affected trees. The GUI keeps native maintenance status/control available
while scrub/balance runs on its worker.

Rollback retains both data versions and has explicit staged/exchanged/saved
journal phases. Its saved path is the old subvolume, not a deleted backup.
It does not silently change rootflags, the default subvolume, BLS/UKI or boot
selection. Verify those separately before booting the replacement. Scrub,
balance and resize are filesystem maintenance, not raw-byte rollback jobs.
Interrupted maintenance requires status inspection and explicit control; it
is never restarted as if an interrupted ioctl completed.

Rename persists its intent before the atomic no-replace operation and syncs the
parent directory before recording completion. Recovery verifies the same source
at the old name or the same UUID/inode at the reviewed new name; unrelated paths
are refused. Renaming does not update fstab, rootflags, BLS/UKI or other boot
references. Review those separately before booting the renamed subvolume.

```json
{"schema":1,"action":"rename","path":"home","new_path":"home-previous"}
```

Example rollback request:

```json
{"schema":1,"action":"rollback","path":"root",
 "snapshot":"snapshots/before-repair","saved_path":"root-before-rollback"}
```

```sh
uke-recoveryctl btrfs plan request.json --root MOUNTED_TOP_LEVEL \
  --profile global-os3.0.303.0 --output plan.json
uke-recoveryctl btrfs execute plan.json --root MOUNTED_TOP_LEVEL \
  --journal OUTSIDE_AFFECTED_TREES --confirm REVIEWED_PLAN_SHA256
```

## Subvolume backup

Backup creates a read-only snapshot with private staging, atomic publication,
UUID checks and resumable state inspection. Full/incremental send uses retained
read-only source and parent snapshots, a native ioctl worker and a bounded
pipe. Send protocol 1 records are checked for CRC32C, TLV widths, safe paths,
source/parent UUID and transaction lineage, complete END and SHA-256. External
clone sources and unknown protocols are rejected. Incomplete captures restart
from byte zero; a verified completed stream is reused.

```sh
uke-recoveryctl btrfs snapshot-plan root snapshots before-repair \
  --root MOUNTED_TOP_LEVEL --profile global-os3.0.303.0 --output SNAPSHOT_STORE
uke-recoveryctl btrfs snapshot-execute SNAPSHOT_STORE --root MOUNTED_TOP_LEVEL \
  --confirm SNAPSHOT_PLAN_SHA256
uke-recoveryctl btrfs send-plan snapshots/before-repair \
  --root MOUNTED_TOP_LEVEL --profile global-os3.0.303.0 --output SEND_STORE
uke-recoveryctl btrfs send-capture SEND_STORE --root MOUNTED_TOP_LEVEL \
  --confirm SEND_PLAN_SHA256
uke-recoveryctl btrfs send-verify SEND_STORE
```

An optional second positional snapshot in `send-plan` selects its incremental
parent. Stores are private and outside the source/parent trees. Nested
subvolumes are not included implicitly. Stream verification does not perform
receive or compare a restored tree. Native receive/restore, complete human
subvolume paths, shipping-kernel support and both tablets' hardware acceptance
remain open.

`tests/check-btrfs-vm.sh` uses an explicitly built, uninstalled native fixture,
a generic ARM64 virt kernel and a newly created 512 MiB file-backed disk.
It never attaches a host block node. The resulting record identifies kernel,
fixture ELF and runner hashes and labels all results as emulation. It cannot
authorize stock-kernel module loading or physical partition writes.

Primary interfaces: [Btrfs ioctls](https://btrfs.readthedocs.io/en/stable/btrfs-ioctl.html),
[subvolumes](https://btrfs.readthedocs.io/en/latest/btrfs-subvolume.html) and
[scrub semantics](https://btrfs.readthedocs.io/en/latest/btrfs-scrub.html).
