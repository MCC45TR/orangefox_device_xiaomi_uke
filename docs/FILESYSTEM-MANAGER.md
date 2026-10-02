# Filesystem management

The Filesystems page and `filesystem` CLI use a shared native plan, private
staging image, independent checker and raw restore journal. A plan names one
target, one action, the existing content hash, filesystem type, firmware
profile, requested size, required space and data-loss choice. Editing the GUI
selection invalidates review. Apply requires the exact reviewed plan hash.

| Filesystem | Format and offline check | Repair | Resize |
| --- | --- | --- | --- |
| ext4 | mke2fs; e2fsck without writes | e2fsck on the private staging image | resize2fs after a staged check |
| F2FS | source-built make_f2fs and fsck.f2fs | fsck.f2fs on staging | resize.f2fs when the selected tool accepts the geometry |
| FAT16/FAT32 | mkfs.fat creates FAT32; fsck.fat checks | fsck.fat on staging | conditional fatresize adapter; tool is not in the shipping recovery |
| exFAT | mkfs.exfat; fsck.exfat without writes | fsck.exfat on staging | unavailable; a file-preserving copy/recreate workflow is still required |
| NTFS | mkfs.ntfs; limited ntfsfix metadata check | limited ntfsfix repair | ntfsresize preview, then staged resize |
| Btrfs | conditional mkfs.btrfs and btrfs-progs checker on the host | use the mounted native manager | use the mounted native manager |

Availability comes from `filesystem capabilities`, not from the table alone.
NTFS validation does not replace Windows chkdsk. Btrfs formatting/checking tools
are not packaged in this stock-kernel recovery. The native Btrfs manager does
not invoke those tools. [BTRFS-MANAGER.md](BTRFS-MANAGER.md) explains its kernel
requirement and operation scope.

The source target is never passed to a write tool. The native engine reflinks
or copies it to a private regular file, runs the formatter/repair/resizer there,
syncs, verifies its signature and checks through a read-only descriptor. It
then creates verified desired data and original/target recovery mirrors before
the raw journal writes the original object. Failure during staging leaves that
original unchanged. Failure after application begins requires journal inspection.
Rollback verifies actual bytes and restores the complete original image.

Image operations retain the original inode and capacity. A filesystem resize
does not move a partition or change GPT boundaries. Some tools truncate their
working image; the engine restores the original capacity without extending the
filesystem. FAT resize gives fatresize a temporary MBR partition envelope,
copies back only the checked filesystem and preserves original hidden-sector
metadata. Its result must fit within the requested size and the reviewed FAT32
cluster minimum; implicit FAT16/FAT32 conversion is rejected. The envelope
adapts the tool's partition interface and inclusive end calculation; see the
[upstream adapter source](https://github.com/ya-mouse/fatresize/blob/ab78c48/fatresize.c).
NTFS resize refreshes its alternate boot sector at the retained container end
before the independent read-only ntfsfix check; filesystem identity is preserved.

The GUI accepts GB, GiB, MiB and percentages of the selected container for the
size. The CLI request uses exact bytes. Containers are bounded to 32 MiB–512 GiB
and requested sizes are multiples of 4 KiB. A tool may reject a smaller size
because of its own filesystem minimum or occupied data. Such failure does not
authorize writes to the source. Plans reserve five complete container sizes
plus 64 MiB for normal jobs, and six plus 64 MiB for a FAT resize envelope.
The native copying buffer is 1 MiB; external tools have separate memory needs.

For a disposable regular image:

```sh
uke-recoveryctl filesystem capabilities
uke-recoveryctl filesystem plan request.json --image filesystem.img \
  --profile global-os3.0.303.0 --output plan.json
uke-recoveryctl filesystem execute plan.json --image filesystem.img \
  --journal journal --confirm REVIEWED_PLAN_SHA256
uke-recoveryctl filesystem inspect-journal journal --image filesystem.img
uke-recoveryctl filesystem rollback journal --image filesystem.img \
  --confirm REVIEWED_PLAN_SHA256
```

A format request contains `schema: 1`, `action: "format"`, the `filesystem`,
`erase_confirmed: true` and optional ASCII `label` of at most 11 characters.
Repair uses `action: "repair"`; resize uses `action: "resize"` and
`target_bytes`. Unknown or unrelated request fields/options are rejected.
Staging cannot be resumed by pretending its tools completed. Inspect or cancel
that store and create a new plan. Once the application journal exists, explicit
resume/rollback/cancel use its verified current-byte classification.

## Device boundary

`storage preflight --object STABLE_ID --profile PROFILE` checks the real kernel
root, retained block claim, storage identity, LUN, all visible mount namespaces,
process holders, swaps, USB exports, unlocked Uke boot identity, current slot,
merge state, inactive-slot fallback and pinned Global boot-stack hashes. An
image or fake proc/property tree cannot substitute for this evidence.

The final live writer/range/SKU acceptance gate remains closed. Real block
format/repair/resize and coordinated filesystem/GPT application are unavailable
in this candidate. Whole devices, GPT/protective-MBR images, encrypted containers, Android
userdata/metadata and OEM firmware partitions are rejected as filesystem targets.
The stock recovery installer has its own active-slot policy. Neither Pad 7 nor
POCO Pad X1 has physical storage or power-loss acceptance for these new jobs.
