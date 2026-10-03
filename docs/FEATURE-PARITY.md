# Nabu recovery feature parity for Uke

Six branches of [ArKT-7/twrp_device_xiaomi_nabu](https://github.com/ArKT-7/twrp_device_xiaomi_nabu) are archived with full selected history. Their exact commits are in the workspace source catalog. [The inventory](../reports/nabu-feature-inventory.json) records source files and 686 relevant UI actions. This is a source review; donor scripts were not executed.

| Branch | Pinned commit | Role |
|---|---|---|
| mod-windows | b7dc2d2c2285630744934ffdd387bddd69a723de | Current UI, Windows/GPT tools and return path |
| mod-linux | 91e6e861c68025e838a382f75e57dd2e321ca2f1 | Linux partitions, ESP and chroot |
| mod-uefi-dualpart | dd50416e860f274806b6069e24734c95868b2ffe | Two Android installations and UEFI switching |
| mod-normal | ed558b4ae201651ec786d25d8869802660838076 | Recovery and OTA extraction |
| mod-win-CN | 31e31389e1a54d72776b8ad22cefcae3f55ebf8b | Language and regional UI differences |
| test-rotation | 639290964d6a1cc36d98328984fdcd2e83dfbfcd | Rotation experiments |

The table states acceptance targets, not completed hardware support. A
host-tested Linux/ESP identity and read-only mount control now covers part of
REC-13/14. Distribution-aware managed chroot and staged filesystem-image jobs
have native implementations; live storage writes remain gated. Windows and
second-Android tools require working platform support and proven isolation.

| ID | Capability | Nabu source | Uke acceptance |
|---|---|---|---|
| REC-01 | ZIP/image installation | Recovery UI | Model, hash, size, target and readback checks |
| REC-02 | Backup and restore | Standard UI | Partition/firmware identity and corrupt-backup rejection |
| REC-03 | File manager and terminal | portrait.xml, twres-arkt | Touch/key navigation, mount permissions and exit status |
| REC-04 | Four display orientations | Rotation UI and test-rotation | Rendering and touch at 0/90/180/270 degrees |
| REC-05 | Language selection | languages/ and CN branch | English/Turkish readability and layout overflow checks |
| REC-06 | Brightness, display and battery | BoardConfig/UI | Correct backlight range and telemetry units |
| REC-07 | ADB and sideload | USB init/UI | Enumeration, transfer checksum and reconnect |
| REC-08 | MTP and USB mode selection | USB init/common | Single owner, clean transitions and FBE-aware visibility |
| REC-09 | Fastbootd | Device flags/UI | Correct logical partitions/slots; distinct from bootloader fastboot |
| REC-10 | Log export | Recovery UI | Build/session identity and redacted export |
| REC-11 | Android decryption | Keymaster/gatekeeper services | Installed Uke firmware, ABI and TEE trust evidence |
| REC-12 | Keymaster recovery diagnostics | restore_keymaster action | Compatibility diagnosis; no copied Nabu keys or security binaries |
| REC-13 | Partition and write-lifetime inventory | checkpartitionlist/lifetimewrites | Real GUIDs, labels, sizes, units and health interfaces |
| REC-14 | Linux space planning | mod-linux partition | Synthetic overlap/overflow tests and live-plan identity matching |
| REC-15 | ESP/Linux formatting | format, formatdata, mkfs.ext4 | Correct target/filesystem and readback on test media |
| REC-16 | Linux chroot | lon-chroot | Controlled bind mounts, return codes and cleanup |
| REC-17 | Return to stock layout | restore/stock_gpt | Uke-specific verified GPT and stock images |
| REC-18 | GPT backup/restore | gpt0..5, restoregpt, sgdisk | Match LUN, GUID and capacity; reject another device's backup |
| REC-19 | GPT repair | fixgpt | Read-only diagnosis and primary/backup comparison first |
| REC-20 | Resizing tools | parted, sgdisk, partitionByArKT | Same CLI/UI plan; reject insufficient space and overflow |
| REC-21 | Boot/AVB inspection and repair | fixvb, recovery patch | Profile-correct headers and integrity; no automatic AVB disabling |
| REC-22 | USB mass storage | msc, mscbyarkt | Selected image, default read-only, mutually exclusive host/local writes |
| REC-23 | Windows/ESP formatting | format, formatwinesp | Conditional NTFS/FAT support and explicit target plan |
| REC-24 | WIM backup | backupwin, wimlib | Metadata, space, checksum and test restore |
| REC-25 | WIM/ESD restore | restorewin, wimlib | Correct image/index, size and isolated test partition |
| REC-26 | NTFS repair/resize | ntfsfix, ntfsresize | Tool provenance/licenses and damaged-image tests |
| REC-27 | Panel identification/settings | panel | Uke-specific identity; no Nabu panel commands |
| REC-28 | UEFI selection and reboot | uefi.img, rebootbyarkt | Source-built Uke Aloha, artifact manifest and Android return |
| REC-29 | Second Android space | dualpart | Explicit metadata/super/userdata isolation |
| REC-30 | Android installation switching | switch | Recoverable transitions and interruption tests |
| REC-31 | OTA payload extraction | otaripper, pdumper, ziptool, pv | Metadata, traversal prevention, slot and firmware matching |
| REC-32 | Dynamic partition tools | lpmake, dmsetup | Reject conflicts with active snapshot merge |
| REC-33 | Property and repack tools | overrideprops/build patch | Accurate platform metadata; no fake security claims |
| REC-34 | Reproducible CI | Workflows and patches | Pinned inputs, reviewed artifacts and full build logs |

Standard OrangeFox features remain part of the target. The management UI shares preflight validation and diagnostics across them. Root or kernel-modification add-ons are not installed automatically.

The stock Global kernel has Btrfs disabled, so Btrfs mounting is not supported
by the current recovery build. Native snapshot, send and maintenance operations
require a matching Btrfs-capable kernel. OrangeFox's generic flashlight control is disabled for Uke until a
real, safe LED path is verified on each model. See [Linux/ESP tools](LINUX-ESP-TOOLS.md).

## URE capability extension

The following 24 contracts extend REC-01–REC-34 without replacing the donor
minimum. They cover the supplied [comprehensive roadmap](COMPREHENSIVE-ROADMAP.md)
and link to the workspace's URE-00–URE-15 milestones. The [native checkpoint](URE-NATIVE.md)
records source and host-tested parts of C01–C08/C14–C16/C18/C20/C23. Native
Btrfs snapshot/send/maintenance code also extends C12/C13 conditionally. Bounded
usage/mapper policy and identity-bound storage stream software extend C01/C18;
native firmware/slot/snapshot and namespace/holder preflight now extends C01,
while live writer/range/SKU acceptance and managed USB/SSH remain unfinished. The full contracts
below remain acceptance targets; no whole contract is complete. Optional P3 work remains optional.
Physical results continue to use the workspace hardware ledger and test records.
The [filesystem/rescue checkpoint](../reports/URE-RESCUE-FILESYSTEMS-BUILD.md)
records the new host/runtime scope and separate generic-kernel Btrfs emulation.

| ID | Capability / roadmap sections | Phase / priority | Acceptance contract and current dependency |
|---|---|---|---|
| URE-C01 | Native shared library, structured API and Storage Graph (§2–6,71) | URE-01 / P0 | Preserve current CLI behavior, bound JSON, identify every parent LUN/GUID/range/owner and reject duplicates at U0/U1; library/API and a bounded graph now have host fixtures; full live identity closure remains open. |
| URE-C02 | Durable transaction engine (§7,48–49,67–69,90) | URE-03 / P0 | Reject stale plans and changed artifacts; verify backups, journal boundaries and readback; file/GPT/raw-image/combined partition engines have host fixtures, including actual SIGKILL and EFBIG uncertainty/resume/rollback. General live writes and exact-device forced-reboot acceptance remain open before H2. The owner clarified forced reboot as the primary interruption scenario on 2 October 2026; historical power-loss records retain their original scope. |
| URE-C03 | Recovery diagnostics and report export (§31–44,64–66,86,95) | URE-02 / P0 | Correlate R000–R100 stages, available pstore and kernel/module/display/touch/USB/UFS/power evidence; preserve crash records, bound collection and test public redaction at U1/H1. |
| URE-C04 | Linux distribution/package discovery (§10,39,42,53–55,70) | URE-05 / P1 | Bound os-release/package-db parsing, support multiple roots, distinguish observations from hypotheses; do not claim current partition fixtures prove distro detection. |
| URE-C05 | Installed Linux kernel and boot consistency (§10,25,52–54) | URE-05,URE-09 / P1 | PARTIAL source/host: bounded installed Image/bzImage, native gzip/XZ/Zstd initramfs, module ELF/vermagic/dependencies, FDT and origin-bound BLS/UKI audit; userspace/kernel architecture, embedded release and stable root-selector checks. Signatures, complete initramfs executable closure, actual module ABI, hardware DT matching and boot execution remain open. |
| URE-C06 | Native GUI text editor and config validation (§12,61–63) | URE-05 / P1 | UTF-8, bounded files, search/replace/undo, binary rejection, external-change detection and backup; atomic fsync save preserves metadata and validates fstab/crypttab/BLS at U1/H2. |
| URE-C07 | Linux files, snapshot compare and bounded search (§13,59–63) | URE-05 / P1; forensics P3 | Preserve UID/GID/mode/ACL/xattrs/SELinux/capabilities, safe symlink rules and checksums; snapshot file restore/search/diff use explicit sources and destinations. |
| URE-C08 | Controlled chroot and Fedora rescue (§11,54–55,63) | URE-05 / P1 | PARTIAL source/host: automatic Arch/Fedora/Debian/Alpine dispatch, reviewed fstab and selected ESP mounts, isolated mount/PID/network namespaces, native shell/interpreter checks, package-check, Fedora rpmdb/SELinux and Fedora/Arch initramfs dispatch, retained supervisor and verified descendant cleanup/timeout. Host namespace integration passes; real package database/initramfs repair and tablet acceptance remain open. |
| URE-C09 | Recovery kernel capability expansion (§9,16,50–51) | URE-06 / P1 | Profile-matched kernel/modules expose actual filesystem/crypto/gadget/pstore capabilities; U1/U2 images precede H1; current Btrfs mount is BLOCKED by stock kernel. |
| URE-C10 | LUKS/dm-crypt lifecycle (§14,30,85) | URE-07 / P1 | Explicit unlock/lock, metadata and header backup; wrong-key/header/mapping fixtures, bounded zeroed secret input without argv/log leakage; advanced key changes require H2. |
| URE-C11 | Existing BitLocker volume access (§15,20–23,85,98) | URE-07,URE-12 / P2 | Validate pinned BITLK/kernel crypto, supported password/recovery/startup-key inputs and RO first; no TPM/SmartCard/header-edit promise; wrong keys and malformed volumes at U1 before H1/H2. |
| URE-C12 | Btrfs subvolumes, snapshots and rollback (§16,60,97) | URE-08,URE-09 / P1 | PARTIAL native implementation: bounded ioctl inventory, subvolume creation/deletion with retained backup, read-only flags/snapshots and journaled atomic rollback retaining the original; mount/FD/process-root/cwd ownership checks. Complete human paths, boot integration and tablet acceptance remain open; shipping kernel remains BLOCKED. |
| URE-C13 | Btrfs scrub, balance, streams and expert rescue (§16,29,67–68) | URE-08 / P1; expert rescue later | PARTIAL native implementation: bounded filtered balance/status/pause/cancel, device scrub/status/cancel, mounted single-device resize and full/incremental RO snapshot send with protocol/CRC32C/lineage/SHA checks. Receive/restore and expert rescue remain open; verification-only scrub needs an RO mount and shipping kernel remains BLOCKED. |
| URE-C14 | GPT, filesystem framework and boot-chain backups (§8–9,17–20,30,58,75–77) | URE-04 / P0; write tools follow URE-03/06 | PARTIAL source/host: GPT image repair/restore, staged filesystem adapters, combined userdata-only filesystem/GPT jobs and a coordinated six-LUN stock image job with selected pinned OS payloads, native sparse decoding, protected ranges, durable before/after journals and inspected rollback. Firmware/slot/merge/holder preflight exists. Live writes, encrypted migration and model/SKU acceptance remain open; exFAT resize, complete NTFS repair and EROFS mutation are unavailable. See [stock image scope](STOCK-IMAGE-RESTORE.md). |
| URE-C15 | Android A/B, Virtual A/B, super, OTA and FBE (§24,41,76–77) | URE-11 / P1 | Distinguish slot/snapshot/logical metadata, reject active or unknown merges and unsafe OTA inputs; installed-firmware KeyMint/TEE trust plus credential/RO evidence required; FBE BLOCKED. |
| URE-C16 | Direct one-shot OS boot and Aloha contract (§25–27,52,70) | URE-09 / P1/P2 | Versioned exact target/entry IDs, validated boot components, consumed request, default preservation and Android/recovery fallback; malformed/stale/replayed requests at U1 and real routing at H2. |
| URE-C17 | Boot history and known-good restoration (§26,75,95.7) | URE-09 / P1 | Correlate request/acknowledgment/crash/re-entry; no invented successful boot; restore named stock/Linux/Windows boot sets only after matching identity and backup verification. |
| URE-C18 | USB network, SSH/SFTP and host backups (§28–29,56–58,83–85,99) | URE-10 / P1/P2 | Opt-in key authentication, visible fingerprint, reviewed SFTP helper, exclusive gadget/storage ownership, chunk/final hashes and reconnect tests; U1/U2/H1/H3 without exposing unlocked content implicitly. |
| URE-C19 | Wi-Fi rescue (§28.5,83–85) | URE-14 / P2 | Matched WLAN firmware/driver/regulatory state, secure credentials, DHCP/static addressing, forget and long-transfer/reconnect evidence; separate from USB-network acceptance. |
| URE-C20 | Windows detection, NTFS/WIM and ESP/BCD rescue (§20–23,40,98) | URE-12 / P2; rich BCD editing P3 | Reliable offline edition/build or unknown; metadata-preserving WIM/ESD and NTFS fixtures, bounded archive extraction, isolated H2 restore, BCD/Microsoft EFI backups; ntfsfix is not chkdsk. |
| URE-C21 | Multi-OS layout designer and shared ESP (§8.4,70–74) | URE-13 / P2 | PARTIAL source/host: original-userdata-only GB/GiB/MiB/percentage planner, role/filesystem selection, graph/review GUI, advanced GUID requests and front erase/recreate policy; combined filesystem/GPT image commit, inspected resume/full rollback and exact preservation of an existing shared ESP. Existing OS migration, ESP boot registration, multi-LUN orchestration and model-specific H2 acceptance remain open. Other-partition content changes require a separate workflow. See [manager contract](PARTITION-MANAGER.md). |
| URE-C22 | Profiles, signed recovery update and provenance (§78–80,87–94) | URE-00,URE-15 / P0/P1 | Separate model/SKU/region/DTBO/ABI evidence, source/toolchain/config hashes, transitive licenses/SBOM, target no-Python/privacy checks and old recovery preservation; support needs exact physical gates. |
| URE-C23 | UI profiles, session/long-operation management and guided tests (§44–49,64–69,95.4–7) | URE-02/03/05/15 / P1 | Detected-system/risk/lock state, safe vs engineering profiles, progress and explicit cancel boundaries, mount/mapper/chroot/remote-client cleanup, self-test and per-test result manifests. |
| URE-C24 | Optional extensions and later rescue tools (§81–82,89,92,95) | URE-15 / P3 | Permissioned reviewed extensions, QR address/fingerprint without secrets, local remote UI, serial evidence, extra distro/filesystem/forensic modules; parser fuzzing and privacy/security review before exposure. |

Read/source/build/host/emulation/device evidence stays independent. No row
becomes SUPPORTED through tool packaging or menu presence. Broad transaction
and UI integration require new implementation work; current generic upstream
actions are not universally protected by the native installer.

## Portability findings

`common`, `partition`, `dualpart` and `switch` use Nabu partition numbers and fixed offsets. Sourcing common code can unmount filesystems or manipulate device-mapper state. Those files cannot serve as a read-only Uke discovery library.

The mass-storage helpers can expose an entire UFS LUN, set SELinux permissive and select a fixed UDC address. Uke requires an explicit target, validated controller and ownership lock. Bundled GPT backups, keymaster binaries, kernel images and UEFI payloads are reference material only.

The switch path renames partitions and writes boot payloads. Its replacement needs interruption, backup, slot and snapshot tests. This review is not a completed security audit; storage-changing implementations require their own code review and synthetic tests.
