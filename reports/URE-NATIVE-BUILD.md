# Native URE candidate build

30 September 2026. This is a local experimental implementation checkpoint for
**Global OS3.0.303.0.WOZMIXM**. It does **not** complete the request to implement
every feature in the supplied roadmap. Additional source work and physical
acceptance remain necessary. Nothing has been flashed or published by this
change.

This report records the 30 September checkpoint. Subsequent journal recovery
and chunked backup implementation is recorded separately in
[URE-STREAMING-BUILD.md](URE-STREAMING-BUILD.md); its artifact hashes differ.

## Built implementation

The C++20 library is statically linked into `uke-recoveryctl` and the OrangeFox
GUI. It provides bounded Storage Graph discovery, GPT/image inspection,
Linux/Windows installation discovery, file metadata/search, UTF-8 editing,
configuration syntax validation, regular-file save/backup/journal/rollback,
boot-component discovery/request planning, private diagnostics and public
allowlist reports. The GUI adapter adds native pages under Advanced → Uke
Recovery Environment. GUI rendering and touch operation have not been tested.

Source-built NTFS/exFAT/F2FS tools, wimlib-imagex 1.14.5 and Dropbear 2025.89 are
included. Dropbear has password/PAM authentication, forwarding and SFTP disabled;
it is not started automatically. Its presence does not implement the managed
SSH/USB session contract. WIM capture/apply and filesystem formatting are tested
only with disposable host files; managed device restore/resize/format
transactions remain absent.

The scope and limits of each actual CLI interface are in
[URE-NATIVE.md](../docs/URE-NATIVE.md). All supplied topics 0–102 and phases 0–15
remain in [COMPREHENSIVE-ROADMAP.md](../docs/COMPREHENSIVE-ROADMAP.md). The 34
original minimum feature rows and 24 URE contracts remain in
[FEATURE-PARITY.md](../docs/FEATURE-PARITY.md). No full roadmap phase or contract
is declared complete.

## Verification evidence

| Class | Result and boundary |
|---|---|
| Root host checks | 19 checks pass: source catalog, hardware-evidence policy, archive/offline-restore rejection cases, privacy and 100-step dependency ordering |
| Native host tests | CMake/CTest, CLI JSON and invalid-option cases, stale/confirmed save plans, metadata/xattr preservation, backup/readback, rollback, Linux/Windows discovery and legacy mount-plan fixtures pass |
| Parser tests | Valid/corrupt 512/4096-byte GPT, protective/hybrid MBR geometry, metadata overlap, zero/case-insensitive duplicate identities, sysfs escapes, invalid JSON/UTF-8 and filesystem UUID byte order pass |
| Installer fixtures | Slot/snapshot/stock-hash/fallback/copy/readback/host-refusal cases pass; production block writes are never invoked |
| Sanitizers | Android-host Clang ASan and UBSan pass the C++/CLI fixtures; `vptr` is excluded because the bundled sanitizer runtime lacks its C++ handlers. This is not fuzzing or target instrumentation |
| Android source build | Locked OrangeFox Android 16 target `twrp_uke-bp2a-eng recoveryimage` completes at neutral `/mnt`; native library, GUI adapter and additional tools compile |
| Extracted payload | Actual header-v4 LZ4 ramdisk is extracted in a restricted namespace. Privacy/no-Python checks, renamed ZIP recognition, bounded recursive archive scans, GUI XML/tool-manifest checks and all 205 AArch64 ELF interpreter/dependency checks pass |
| AArch64 QEMU userspace | Native CLI save/backup/rollback and legacy identity fixtures, WIM capture/verify/info/apply round trip and truncated-image refusal, ext4/exFAT/NTFS no-action checks with unchanged hashes, and ephemeral SSH host-key generation pass |
| Package | IMG sizes/headers, exact stock kernel, shared ramdisk, static installer, ZIP recovery entry/integrity and repeated packaging hashes are checked separately |
| Source identity | Original tool pins/licenses, reviewed patches, project adapters and exact input-file hashes accompany the candidate; reference acquisition/restore succeeds for all three new tool archives |
| Physical hardware | **UNTESTED**: boot, screen, touch, USB/network, mounted filesystems, slot HALs, encryption, installed systems, storage writes and rollback |

QEMU is user-mode emulation using synthetic directories and regular image files.
It does not boot this recovery kernel, prove firmware/DTB/module compatibility,
start an SSH listener or operate on a tablet. The mke2fs fixture explicitly uses
the packaged configuration to avoid importing a newer host filesystem policy.

## Artifacts and provenance

The local output directory is `artifacts/ure-native-alpha/`; the earlier public
alpha in `artifacts/prerelease/` remains intact and its existing SHA256SUMS pass.
The candidate contains:

- `OrangeFox-uke-recovery.img`: 104,857,600-byte dedicated recovery, header v4,
  zero embedded kernel.
- `OrangeFox-uke-fastboot-boot.img`: 100,663,296-byte temporary-boot candidate;
  must never be flashed.
- `OrangeFox-uke-flashable.zip`: project-owned active-slot installer, preserving
  inactive stock recovery after both stock boot stacks pass the hash gate.
- `ARTIFACT-MANIFEST.json`, `SHA256SUMS`, AVB descriptors, payload inventory,
  extracted-ramdisk/QEMU and native-host verification records, project/test input
  hashes, tool pins/licenses, installation/stock-return instructions and source
  snapshots.

The temporary image uses the unmodified 35,432,960-byte stock kernel with SHA-256
`97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9`.
Both image roles use the same compressed ramdisk. AVB is `NONE`, the ZIP is
unsigned and hardware rollback is untested. CN, Turkey, other firmware and
cross-model compatibility are not accepted targets.

Manifest schema 2 identifies the base commit separately from working-tree
changes. `PROJECT-INPUTS.sha256` records the actual source/configuration/patch/
script/test files used by this checkpoint. The upstream OrangeFox core is
`3d733672081bca3af42475a286145f4a8cdce4e7`; 399 Android projects retain exact
pins. NTFS/JsonCpp/WIM/Dropbear pins are in `URE-TOOLS.json`. Corresponding source
snapshots preserve upstream license notices and carry reviewed patches and
project build inputs. They do not archive all 399 repositories or prove an
independent byte-for-byte rebuild. Repeating packaging of one built image is a
separate, narrower result.

## Remaining implementation and acceptance

File transactions cover existing regular UTF-8 text files up to 1 MiB. GUI
journals are volatile `/tmp` records. Persistent power-loss recovery, streaming
backup, block-device transactions, GPT repair/layout changes, managed formatting
and resizing remain unimplemented. Advisory locks do not establish exclusive
control over unrelated writers.

Controlled chroot/package/initramfs repair, full Linux kernel/UKI/DT root
consistency, registry/BCD rescue, WIM-to-device restoration, multi-OS layouts,
managed SSH/SFTP/USB export, Wi-Fi, long-operation cancellation/resumption,
signed updates, plugin/extension management and full reproducibility/fuzzing
still need source work. No target Python execution path is introduced.

Cryptsetup is archived but not packaged. Btrfs userspace is not packaged and
the stock recovery kernel has Btrfs disabled. LUKS/BITLK lifecycles and Btrfs
snapshots/maintenance/send/receive are therefore unavailable. FBE remains
disabled pending firmware-matched installed KeyMint/TEE trust. One-shot
Linux/Windows boot execution is refused until an accepted Uke stock/Aloha/UEFI
backend exists. Missing prerequisites are not replaced with Nabu hardware
assumptions.

The project installer retains its Uke checks. Generic upstream terminal,
partition and tool actions are not universally covered by the native file
transaction policy; this candidate is an engineering artifact, not a supported
end-user recovery release.
