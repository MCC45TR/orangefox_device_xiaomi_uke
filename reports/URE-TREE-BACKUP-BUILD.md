# Linux and home tree backup checkpoint

1 October 2026. Local unsigned Global OS3.0.303.0.WOZMIXM candidate. GitHub
publication remains deferred. This extends Linux/home backup software without
claiming complete roadmap, Btrfs snapshot/send/receive, physical backup or
hardware rollback acceptance.

The URE menu now provides **Linux and home directory backup / restore**. GUI
and CLI share the native tree engine: plan/review, capture with exact plan
confirmation, file-boundary resume, independent verification and restore into
a new directory. It preserves sparse data, opaque filenames, in-tree hardlinks,
symlink targets, UID/GID/mode, nanosecond mtime and readable xattrs/ACLs. FIFO and
device-node metadata are recorded; foreign ownership/device-node restoration
requires root. Runtime sockets are counted and recreated by services rather
than copied. See [TREE-BACKUP.md](../docs/TREE-BACKUP.md) for usage and limits.

The source must already be accessible. The engine performs no mount, unlock,
chroot or installed-program execution. Nested mounts/subvolumes, observed
Android/calibration aliases, pseudo-filesystems, recursive destinations,
changed sources and malformed/corrupt archives refuse. Capture observes source
namespace/metadata stability and verified file hashes; it is not an atomic
snapshot or hostile-writer lock.

Metadata uses hash-bound bounded pages. Data hashes cover logical bytes and
reported sparse extents avoid copying holes. Content blobs deduplicate without
turning separate restored files into hardlinks. Transfer buffers are 64 KiB;
directory validation uses a depth-first stack, hash caches are capped and
directory metadata is restored by reading pages backwards. Incomplete files
are recopied while published files are verified/reused. Capture state is
atomic and can be inspected while the writer holds its lock.

Restore verifies the complete backup before creating sibling staging. Data and
metadata pass readback, directories are finalized after children and a no-replace
rename publishes the result. Existing destination files are not overwritten.
Failure distinguishes retained staging from an already published destination
whose directory sync is uncertain. General staging recovery, selective/in-place
restore, compression and direct host tree streaming remain unfinished.

| Evidence class | Verified result and limits |
|---|---|
| Root host suite | All 19 checks and proposed-index privacy/no-reply identity checks pass |
| Native host suite | All 13 CTest executables and complete CLI fixtures pass against exact source receipts |
| Tree fixtures | Sparse files, binary xattrs, opaque/newline filenames, hardlinks, symlink targets, FIFO, directory mode/mtime, paged manifests, independent verification and isolated restore pass; changed-source/recursive/symlink/confirmation/metadata/data corruption refusals pass |
| CLI interruption | A fixture capture is observed live after blob publication, killed with SIGKILL, inspected and resumed; completed files are reused and the completed backup verifies |
| ACL | Named-user and inherited default ACLs round-trip exactly where available; the isolated QEMU user namespace uses a mapped UID |
| Sanitizers | All 13 executables and the final tree CLI fixtures pass ASan/UBSan with leak detection; the pinned runtime requires excluding vptr |
| Android | Final recoveryimage completes in 2:11; tree library/CLI, GUI actions and menu pages compile for AArch64 |
| Extracted ramdisk | Privacy/no-Python, two recursive ZIP scans, GUI/tool manifest, exact staged binaries and dependency closure for 205 AArch64 ELF files pass |
| Extracted AArch64 QEMU | Actual CLI Linux/home capture/resume/verify/restore fixtures and prior storage/GPT/stream/display/utility fixtures pass; no GUI rendering, filesystem mount or tablet operation |
| Package | Two packaging runs produce identical asset hashes; independent binary reproducibility remains open |

Current local candidate: `artifacts/ure-linux-home-tree-alpha/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Recovery IMG | 104857600 | `fd5c45db8b3447d4f1cf586da5dccbea986153d0877bb1f4131de167bab41f7a` |
| Temporary-boot IMG | 100663296 | `630c6980e0ad3ff538bd370287b313770ffc82b9baa5e8abfb75fdf8f6b20877` |
| Installer ZIP | 32684963 | `697b0a12f523667e7cde22198ff098faf1dcded3340d98f4b17c18da69a091d3` |

Compressed ramdisk: 37437330 bytes, SHA-256
`4d2d923fdb1b5b9917e3f7aaa1f536d86fdff7295206c9ac314b6b2a0cfbff64`.
Extracted native CLI:
`b4176bc309be1442ab006b4f1bb87c95937149e5265e60ed0e4cddc7d626f2a2`.
The original stock kernel remains unchanged. AVB is NONE and the ZIP is unsigned.
The temporary-boot IMG must never be flashed. Preserve firmware-matched stock
recovery and the inactive stock slot according to the candidate instructions.

Root, home, boot and ESP mounts may need separate backups. Existing raw storage streams
provide partition bytes/geometry; neither path proves a bootable restored OS.
Stock-profile Btrfs support, full/incremental subvolume streams, encryption
trust, full partition/filesystem migration and the remaining roadmap continue
to require implementation and separate acceptance gates. No physical success
record has been created.
