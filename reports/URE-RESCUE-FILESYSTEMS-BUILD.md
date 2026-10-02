# Filesystem, Linux rescue and Btrfs checkpoint

2 October 2026. Local unsigned Global OS3.0.303.0.WOZMIXM candidate.
GitHub publication remains deferred. Source, host, Android build, package and
emulation results are recorded separately. Neither Xiaomi Pad 7 nor POCO Pad X1
has a physical boot, storage-write or rollback result for this checkpoint.

The new Filesystems, Linux and Btrfs pages use the C++ management API. Review
seals the operation, selected object/root/ESP, request, source identity and
confirmation hash. Changing a field or selection invalidates review. Tests
compile and execute the actual management callbacks with host UI stand-ins;
this checks routing and review policy, not rendering on a screen.

## Storage and filesystem jobs

Native storage preflight checks the actual recovery kernel root, exclusive block
claim, graph identity/LUN, visible mount namespaces, file descriptors, swaps,
USB exports, unlocked Uke identity, A/B state, inactive fallback and pinned
Global boot-stack hashes. An image or substitute proc/property tree cannot
forge this evidence. The accepted live writer/range/SKU gate is still closed.

Filesystem-image format, repair and resize use a private reflink/copy. External
tools receive the staging descriptor, never the original target. Signature and
an independent read-only checker must agree before verified original/desired
mirrors permit journaled application. The full result is hashed and read back;
rollback restores every original byte while preserving inode and capacity.
Staging failure leaves the original unchanged. A partially applied job requires
inspection and explicit recovery.

Adapters cover ext4, F2FS, FAT and NTFS resize and ext4/F2FS/FAT/exFAT/NTFS
format/check/repair. NTFS checking/repair is limited ntfsfix metadata work and
does not replace Windows chkdsk. exFAT offline resize remains unavailable.
FAT resizing uses an isolated MBR envelope, honors the FAT32 cluster minimum
and rejects implicit conversion to FAT16. NTFS updates the alternate boot
sector at the retained container end before its independent check. F2FS uses
the pinned tool's safe resize mode. Conditional host Btrfs format/check tools
are separate from the native mounted manager and are not shipped.

Containers are bounded to 32 MiB–512 GiB and size requests to multiples of
4 KiB. Native copies use a 1 MiB buffer. Space estimates reserve five container
sizes plus 64 MiB, or six plus 64 MiB for FAT resize. External tool memory is
separate. These operations do not change partition/GPT bounds or migrate
encrypted Android userdata.

## Distribution-aware Linux rescue and boot audit

The selected root's contained os-release aliases choose Arch, Fedora, Debian,
Alpine or generic behavior. Supported fstab mounts and a selected ESP are bound
inside private mount/PID/network/IPC/UTS namespaces. Root/ESP are recursively
read-only by default. No raw block nodes or mount capability are passed to the
installed command. Native executable architecture/hash, reviewed scripts,
fstab and selections are checked again at execution.

Actions include native Bash, package checks, Fedora rpmdb rebuild and restorecon,
depmod for an exact release, and Fedora dracut/Arch mkinitcpio dispatch. Writable
commands are explicit and require independent backups; they do not acquire
global atomic rollback. Python/PyPy hooks and unreviewed aliases are rejected.
A retained supervisor/pidfd controls timeout and descendant cleanup. Console
capture is private and bounded. Network-enabled rescue and a GUI chroot cancel
control remain unavailable. The host Fedora dispatch fixture uses a trusted
native test executable, not a real installed Fedora database repair.

Installed boot inspection reads bounded Image/bzImage headers, newc/CRC
initramfs, gzip/XZ/Zstd streams, module ELF/vermagic/dependencies, FDT and PE UKI
sections and origin-bound BLS assets. All indexed modules within the limits are
inspected; the 256-record output sample cannot hide later failures. Userspace,
kernel and module architecture, initramfs/UKI release, distribution metadata
and stable root selectors are compared. Missing assets, malformed payloads and
stale combinations produce findings. Signatures, full initramfs executable
closure, actual root block equivalence, kernel symbol ABI, Uke DT hardware
matching and successful boot remain separate acceptance work.

## Native Btrfs operations

The manager uses Linux ioctls for inventory/usage/device errors, subvolume
creation, read-only flags, snapshots, deletion retaining a derived backup,
atomic rollback retaining the original, device scrub, bounded filtered balance
and single-device mounted resize. Maintenance status/control remains available
while the GUI worker runs. Ownership checks include visible mounts, descriptors,
process working directories and roots; journals stay outside affected trees.

Backup publishes a read-only snapshot atomically. Full/incremental send verifies
protocol 1 CRC32C, attribute widths, safe paths, source/parent UUID and transaction
lineage, END and full SHA-256. Root metadata permits the kernel's empty path;
file creation, removal, rename/link and data operations still require child
paths. Incomplete streams restart from byte zero; completed verified data can
be reused. Native receive/restore and complete human subvolume paths remain open.

The generic ARM64 VM uses a new 512 MiB regular-file disk and a separately
built uninstalled C++ fixture. It never attaches a host block device. Its
7.2.8 kernel and modules are emulation inputs, not stock recovery replacements.
The preserved Global stock kernel has Btrfs disabled, so these mounted features
remain unavailable in that shipping kernel.

## Verification

| Evidence class | Result and practical limit |
| --- | --- |
| Workspace host | 19 checks passed; no hardware operations |
| Native host | All 17 CTest executables and the full CLI suite passed against the final exact source input receipt |
| Filesystem tools | All six conditional host format/check/byte-exact rollback fixtures passed; ext4/FAT/NTFS/F2FS shrink passed and FAT file content survived resize |
| Rescue integration | Actual isolated mounts, selected ESP, read-only/writable policy, stale-plan refusal, descendant cleanup and timeout passed; real Fedora repair remains untested |
| Boot parsing | Native gzip/XZ/Zstd, concatenated gzip, early-cpio tail, truncated codecs, UKI overlap/FDT corruption and a bad 257th module are checked |
| Sanitizers | All 17 executables passed ASan/UBSan with leak detection; the pinned runtime excludes vptr |
| Android | Synchronized native CLI, actual GUI, codecs and tool dependencies built successfully; the build now rejects stale staged project sources |
| Btrfs VM | All 14 native ioctl fixtures passed on generic ARM64 7.2.8; snapshot, full/incremental send, corruption/ownership refusal, retained-data rollback, delete backup, scrub, bounded balance and shrink/grow |
| Extracted ramdisk | Privacy/no-Python, two recursive ZIP scans, exact staged source/GUI/tools and 206 AArch64 ELF dependency checks passed; extracted CLI gzip/XZ/Zstd and existing journal/GPT/layout/backup/utility QEMU fixtures passed |
| Package | Two final packaging runs produced identical hashes for all three assets and accompanying fixed inputs; recovery image matches its extracted audit |

Final source identity, exact input manifest, tool licenses, emulation records and
asset SHA-256 values are sealed in `artifacts/ure-rescue-filesystems-alpha/`.
Independent full binary reproducibility remains open. The previous
`ure-userdata-layout-alpha` artifacts are preserved.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Recovery IMG | 104857600 | `5737dede15d0eabdf4f29fedc3f5f11296cefb1a77e164756a45ef11254acd12` |
| Temporary-boot IMG | 100663296 | `4d8e95a0681834268afbfb9748c29cbc946feb1e7fa640ed986c4582b5e60222` |
| Installer ZIP | 33538233 | `70253cea1774bba0f9cecc20663ad8f6bcffc491ccfe22a19bcabdf3a87437ff` |

Compressed ramdisk: 38390028 bytes, SHA-256
`bae5d15eba7c157de34ac6d68dd96dd6e03371c3a72dae6f46e4a8a0d13eb87e`.
Extracted native CLI:
`07cba296cfbe7b525ec1e6eb0fcb7e6cabd66ede7317d1c38d9de754ea9b3623`.
Final native/sanitizer input-manifest SHA-256:
`d0e34822b1ac79db44962a8c5f2bf9120b4daa703c3cac5fd8888e23ae37d605`.
Generic VM kernel SHA-256:
`5691fe5c19ea69328a3cf2990f2ccb05f7eac1ccb600a4c291e927c95183d8a4`.
VM fixture ELF SHA-256:
`a0c57bc2137a7d1d22cb0bba7079d92d4214b6c28b4a3ea9b1d1490d79d40a4f`.
The unchanged stock kernel SHA-256 is
`97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9`.

AVB remains NONE and the ZIP is unsigned. The temporary-boot image must never
be flashed. Preserve the firmware-matched stock recovery, inactive stock slot
and documented return route. There is no physical success record, automatic
slot change, super mutation or stock early-firmware replacement in this work.

The complete roadmap and coordinated live filesystem/GPT transaction remain
unfinished. See the [filesystem contract](../docs/FILESYSTEM-MANAGER.md),
[Linux rescue and boot contract](../docs/LINUX-RESCUE-AND-BOOT.md),
[Btrfs contract](../docs/BTRFS-MANAGER.md) and
[coverage ledger](../docs/FEATURE-PARITY.md) for current limits.
