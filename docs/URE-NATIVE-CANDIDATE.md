# Experimental native URE candidate

This local candidate adds the C++ library/JSON CLI, Storage Graph and image
inspection, Linux/Windows discovery, diagnostics/report export, regular-file
transactions, inspected journal recovery, storage usage checks, identity-bound
chunked file/image/live-source backup software, verified raw-image restore and
interrupted resume/rollback, host
reception and native OrangeFox management/editor pages. Source-built NTFS/exFAT/F2FS,
WIM/ESD and key-authenticated Dropbear tools accompany it. SSH is not started at
boot; no host key is persisted. SFTP, cryptsetup and btrfs-progs are not packaged.
Native filesystem staging/journal pages, distribution-aware chroot, installed
kernel/DT/UKI/initramfs/module auditing and native Btrfs snapshot/send/maintenance
pages are included. Btrfs requires a matching kernel; the preserved stock kernel
cannot supply its filesystem mount.
The separate generic ARM64 VM passes 14 native Btrfs operation fixtures. This
emulation result does not prove either tablet or the preserved stock kernel.

The partition layout GUI allocates ESP/Linux/Windows only from the original
userdata extent, using keyboard sizes in GB/GiB/MiB or percentages. Standard
mode preserves existing identities and userdata start. Advanced mode permits
explicit GUID/content requests; placing OS partitions before userdata requires
erase/recreate and warns that Android data will be lost. The image GUI now
reviews a combined filesystem/GPT job: prepare and check all role filesystems,
preserve the original userdata, protect existing ESP/OEM contents and apply
payloads followed by GPT with inspected resume/full rollback. The legacy
`gpt layout-plan` still applies metadata only. Encrypted-data migration,
Android recreate boot compatibility and device writes remain unfinished.
Separate filesystem jobs can format/repair/resize regular images
without changing their container capacity. Read `PARTITION-MANAGER.md` and
`FILESYSTEM-MANAGER.md` before reviewing either operation.

**The complete roadmap is not implemented. Neither tablet model has a boot,
display/touch, storage-write or rollback acceptance record.** These unsigned
artifacts are an engineering checkpoint, not a supported recovery release.

Only **Global OS3.0.303.0.WOZMIXM** is targeted. The stock kernel and boot stack
are preserved. CN, Turkey, other firmware and Android-version combinations are
not targets. Stock Btrfs is disabled; FBE remains disabled pending installed
KeyMint/TEE trust. Linux/Windows boot execution requires an accepted Uke backend.

The three image/ZIP roles, exact installer preflight, active-slot policy and
stock-return instructions are in the accompanying `STOCK-RETURN.md`. Read it
before considering a device experiment. The temporary-boot image must never be
flashed. Preserve the firmware-matched stock recovery and inactive stock slot.

Verify `SHA256SUMS` from the host. A hash/build/QEMU pass is not a tablet boot
result. The build report records the exact scope of each verification stage.

The GUI is under Advanced → Uke Recovery Environment. OS discovery/editor
operations require an already mounted root. Managed chroot can connect reviewed
supported fstab filesystems and a selected ESP inside a private namespace;
there is no decryption, mapper creation or slot change. The editor accepts valid UTF-8 up to
1 MiB with lines up to 8192 bytes. Save requires review and confirmation of the
sealed file plan and preserves ownership, permissions and attributes.

The GUI journal parent defaults to volatile `/tmp`; select a verified persistent
parent before relying on rollback across reboot. Backup plans in `/tmp` contain
only private metadata; captured chunks use the explicitly selected destination.
The CLI can select a persistent journal. Resume requires current identity and
backup verification; it does not replay automatically. Host SIGKILL recovery
passes. The owner clarified on 2 October 2026 that forced reboot is the primary
tablet interruption scenario; exact-device reset durability and hostile
concurrent-write acceptance remain open. Live-source backup software requires complete unit/boot and usage
evidence plus a retained kernel claim; positive tablet acceptance, atomic
cross-boot continuation and live restores remain open. Native Btrfs read-only
snapshots and full/incremental send are separate conditional operations; native
receive and matching stock-kernel support remain open. Upstream
tool binaries expose their own commands; only documented
URE wrappers share the native validation policy.

The GPT pages add private metadata backup/verification/comparison and reviewed
repair/restore/rollback for regular disk images. Live whole-LUN sources are
read-only; native firmware/slot/snapshot and ownership preflight is present,
while live writes remain gated by the unaccepted writer, range/SKU proof and
device acceptance. Plans seal current and desired tables. GPT recovery
verifies original/target data and rejects unrelated changes; resume only finishes
a verified commit record. Encrypted filesystem migration and live forced-reboot
acceptance remain unfinished. Userdata-only layout design, legacy image metadata
application and the separate combined image job are implemented. No GPT write
authorizes stock boot-stack changes.

See `URE-NATIVE.md` in the source snapshot for implemented interfaces and the
remaining work for all 24 contracts. No full roadmap phase is marked complete.

Raw-image restore has separate target/backup selection and review pages. It
preserves current and desired chunks before writes, checks the complete result,
and offers explicit recovery only after inspecting actual bytes. The journal
requires two raw-object sizes plus a 32 MiB margin. A separate host-assisted
image restore path keeps complete original/desired stores on the host and only
bounded verified chunk pairs locally. It requires an explicit host attestation,
reviewed target identity and authenticated transport. See `HOST-RESTORE.md` for
the protocol, trust boundary and reconnect/rollback procedure. Compression and
real block writes remain unfinished. A volatile `/tmp` journal
cannot provide recovery across reboot. Choose a verified persistent destination.
