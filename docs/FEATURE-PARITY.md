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

All rows are **planned, not implemented or tested by this Uke project**. Windows and second-Android tools remain part of the feature target but require working platform support and proven storage isolation before activation.

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

## Portability findings

`common`, `partition`, `dualpart` and `switch` use Nabu partition numbers and fixed offsets. Sourcing common code can unmount filesystems or manipulate device-mapper state. Those files cannot serve as a read-only Uke discovery library.

The mass-storage helpers can expose an entire UFS LUN, set SELinux permissive and select a fixed UDC address. Uke requires an explicit target, validated controller and ownership lock. Bundled GPT backups, keymaster binaries, kernel images and UEFI payloads are reference material only.

The switch path renames partitions and writes boot payloads. Its replacement needs interruption, backup, slot and snapshot tests. This review is not a completed security audit; storage-changing implementations require their own code review and synthetic tests.
