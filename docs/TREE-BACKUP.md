# Linux and home directory backups

The URE menu provides **Linux and home directory backup / restore**. Select an
already mounted directory and a new backup store outside it, then plan and
review before capture. Existing plans can be reviewed before resuming capture
or restoring. A new restore destination must not exist. No mount, encryption
unlock, chroot or installed program execution is performed.

The same native engine backs the CLI:

```sh
uke-recoveryctl backup tree-plan home --root /mnt/linux \
  --profile global-os3.0.303.0 --output /mnt/external/home-backup
uke-recoveryctl backup tree-inspect /mnt/external/home-backup
uke-recoveryctl backup tree-capture /mnt/external/home-backup --root /mnt/linux \
  --confirm REVIEWED_PLAN_SHA256
uke-recoveryctl backup tree-verify /mnt/external/home-backup
uke-recoveryctl backup tree-restore /mnt/external/home-backup \
  --output /mnt/linux/home-restored --confirm REVIEWED_PLAN_SHA256
```

If `/home` is a separate mount, selecting `home` makes that mount the source
boundary. Select `.` with a mounted home root to back up its contents directly.
Nested mounts and Btrfs subvolumes are refused during traversal; select each
separately. A full Linux tree may therefore require separate root, home, boot and ESP
backups. Existing raw storage backup interfaces remain available for complete
partition/image bytes and geometry. File-tree backups do not create a bootable
partition or prove a filesystem snapshot.

The private store contains a sealed `plan.json`, hash-bound metadata pages,
SHA-256-addressed sparse data blobs and durable capture state. Paths, link
targets and xattr bytes use hex encoding so filenames need not be UTF-8 and may
contain newlines. Original file contents remain opaque; nothing in the source
is executed. The store requires mode 0700 and owned, single-link mode-0600
records/data files. It contains private home contents and identities and must
not be published as a build artifact or diagnostic report.

Recorded/restored metadata includes UID/GID, permission/special bits, mtime at
nanosecond precision, all readable xattrs (including POSIX ACLs, labels and
capabilities), hardlink relationships within the selected tree, symlink targets,
FIFO and device-node descriptions. Unsupported metadata is an explicit failure,
not a silent loss. Restoring foreign owners/device nodes requires root; the
target filesystem and privileges must support the recorded attributes. atime,
birth time and ctime are not restored. Runtime sockets are recorded and omitted
on restore because services recreate them; the result reports the count.

`SEEK_DATA`/`SEEK_HOLE` identifies sparse ranges where supported; otherwise the
file uses expanded data. Full logical SHA-256 includes holes. Identical file
contents share a blob; distinct files remain distinct on restore while recorded
hardlinks share an inode. Transfer buffers stay at 64 KiB. Metadata pages have
at most 256 entries and a 3 MiB budget; total entries, page count, depth, directory
width, xattrs and hardlink index have explicit bounds. Directory validation uses
a depth-first stack, data-verification caches are capped, and restore rereads
pages backwards instead of retaining all directory metadata.

Capture checks the selected root/mount identity, namespace, content size,
ownership, mode, modification/change times and xattrs against the reviewed plan.
Each captured file is hashed before publication and source metadata is checked
again; a final namespace/metadata pass precedes COMPLETE. This establishes
observed stability, not an atomic cross-file snapshot or hostile-writer
exclusion. Use an offline/quiescent source. Android userdata, metadata,
calibration and kernel pseudo-filesystem roots are refused, including observed
ancestor/filesystem aliases. The firmware profile is recorded without claiming
that the mounted filesystem proves physical unit/firmware identity.

Reissuing capture verifies and reuses published files. An incomplete private
file is recopied, so resume is at file boundaries. The published plan remains
immutable. `tree-inspect` can read atomically replaced capture progress without
claiming data verification. `tree-verify` requires COMPLETE and verifies metadata
pages and every referenced blob without opening the original source.

Restore verifies the complete backup before creating private sibling staging.
It writes and reads back file data, restores metadata with readback, processes
directories after their children and syncs records. A no-replace rename publishes
the completed tree. Existing destinations, symlink targets, malformed plans and
corrupt backups refuse. A failed/interrupted restore can leave named staging;
the error identifies it, and existing destination contents are not changed.
Restore staging inspection/resume/cleanup, selective/in-place restore,
compression and direct host tree streaming remain future framework work.

Host tests cover sparse files, opaque names, hardlinks, symlinks, FIFO, binary
xattrs, ACLs where supported, directory metadata, multiple pages, refusals,
observed SIGKILL/file-boundary resume and independent restore/verification.
Android compilation and extracted AArch64 QEMU CLI checks are separate gates.
No physical Linux/home backup or rollback is recorded. Stock recovery still
lacks Btrfs kernel support; snapshot/send/receive backups remain unfinished.

API contracts follow [Linux sparse seeking](https://man7.org/linux/man-pages/man2/lseek.2.html),
[mount identity](https://man7.org/linux/man-pages/man2/statx.2.html) and
[extended attributes](https://man7.org/linux/man-pages/man2/getxattr.2.html).
