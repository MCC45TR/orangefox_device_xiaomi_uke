# Host-assisted restore checkpoint

1 October 2026. Scope: **Global OS3.0.303.0.WOZMIXM**, original 103-topic
roadmap plus the expanded partition-manager requirement. This unsigned local
candidate has source, host, build, package and QEMU evidence. No tablet, live
UFS write, GUI rendering, electrical power-loss or hardware rollback result is
recorded. GitHub upload is deferred; the complete roadmap remains unfinished.

## Implementation and limits

The shared C++ engine now restores raw images from verified host backups without
keeping complete original and desired images locally. A sealed plan binds both
chunk manifests to the image identity, geometry, firmware profile and known
disk GUID. The host captures and verifies the complete original backup, verifies
the desired store and supplies an explicitly HOST_ATTESTED receipt. Its checksum
is not a signature or independent proof of continued host persistence.

Each packet carries original and desired chunks. Both are hash-verified, fsynced
and read back before durable intent and target writes. The active pair survives
until inspected recovery permits replacement. The conservative cache estimate
is four chunk sizes plus 16 MiB, independent of full image size. Inspection
prioritizes uncertain active writes, classifies actual bytes and supports
explicit continuation, rollback, full final verification and safe cancellation.
Missing host backups remain a recovery limitation.

Ordinary chunks reuse a compact readback proof bound to the observed full image
identity and metadata, with a selected-chunk current-hash check and subsequent
readback. Changed metadata, invalid proof, uncertainty, explicit status,
reconnect and completion retain full scans. Fast responses declare their scope
and return a null full-image hash. This avoids repeated whole-image I/O per
ordinary chunk without a measured throughput claim. Locks cover cooperating
transactions; they do not exclude hostile writers or create atomic snapshots.

The host Bash workflow provides explicit start/resume/rollback, strict batch SSH
and serial-bound ADB shell-v2 duplex transport with propagated exit status.
Transport fixtures use mocks; no real ADB/SSH connection or listener was tested.
Terminal journals require fresh full-content verification before success.
Native OrangeFox review/inspection/recovery pages compile; rendering is open.
The default `/tmp` journal is volatile. The common write gate limits execution
to isolated regular images and refuses physical writes and live loop aliases.

The [partition-manager contract](../docs/PARTITION-MANAGER.md) extends layout,
filesystem, migration and multi-LUN requirements. The new OEM catalog verifies
the Global archive and thirty GPT/rawprogram/patch input hashes, XML, native
ranges and agreement with 138 stock programming entries over LUNs 0–5. Those
entries include GPT regions and open-ended ranges, not 138 user partitions.
LUN0 userdata starts at sector 2961408; capacity-dependent OEM patches must be
resolved against measured geometry. Native reconstruction and default-layout
restoration remain unimplemented at this checkpoint.

## Validation

| Evidence class | Result and limits |
|---|---|
| Root host suite | 19 source/archive, policy, privacy and dependency-order checks pass |
| Native C++ | Six CTest executables pass, including 512/4096 stream geometry, identity/profile/receipt refusals, locks, bounded cache, cancellation, actual interrupted reception, uncertain partial writes, rollback, divergence and proof invalidation |
| CLI and host workflow | Local/host-assisted restore, complete backups, explicit resume/rollback, terminal verification, malformed input and strict SSH/ADB mocks pass; existing file/storage/GPT and installer fixtures also pass |
| Sanitizers | Pinned Android-host Clang ASan/UBSan with leak detection passes all six executables and stream CLI fixtures; vptr is excluded because the bundled runtime lacks its handlers |
| Android build | Locked OrangeFox Android 16 recoveryimage completes in 2:05; shared engine, CLI and native pages compile for AArch64 |
| Actual compressed ramdisk | Header-v4 LZ4 extraction, privacy/no-Python, two recursive embedded ZIP scans, GUI XML/tool manifest and all 205 ELF architecture/interpreter/dependency checks pass |
| AArch64 QEMU | Local/host-streamed image restore/readback/rollback, duplex mocks and existing file/GPT, backup/receiver, WIM, ext4/exFAT/NTFS no-action and ephemeral SSH-key fixtures pass; host sys/proc is exposed read-only to retain the image-ownership gate |
| Package | Dedicated/temporary roles, stock kernel and ZIP payload agree; two runs from the same built image have identical hashes; independent binary reproducibility is open |
| OEM input catalog | Full archive and thirty input hashes, XML, native range validation and canonical stock-map comparison pass; reconstruction/restore and hardware evidence are separate |
| Hardware | UNTESTED; process/file fixtures and QEMU are not tablet or electrical power-loss evidence |

## Artifact identity

Local candidate: `artifacts/ure-host-restore-alpha/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Dedicated recovery IMG | 104857600 | `f2f5155bb5700afe61ecb9b04ce07c2d6899a9eb623d8b48a25234b6eee4523a` |
| Temporary-boot IMG | 100663296 | `752a6c198c69b3a64290cd1628631c5e3c3353040d7bc7e96f9332cec3bba7c5` |
| Active-slot installer ZIP | 32476997 | `ef9bfd282a48df5bffbc1bea1c9dbe4c6cdcc29d76d62f90727a7dfa52ee6d0d` |

Compressed ramdisk: 37220775 bytes, SHA-256
`aca48a885f65fe3752e7fc1d06f89fb12959475097c0653e408f554e945b4285`.
Extracted AArch64 CLI: SHA-256
`34d1b6905f8314d952cafcb8b80ca458d0ae6491393cfee9955eef2e9752575d`.
Unmodified stock kernel: SHA-256
`97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9`.
AVB is NONE and the ZIP is unsigned. The temporary-boot IMG must never be flashed.
The verified OEM archive has SHA-256
`f811ae6255b7535d32f80548d800487a6494a87ddb4cca592799337fab24cd0d`.
CN and Global profiles remain separate.

## Remaining work

The full partition manager, capacity-derived stock GPT reconstruction, multi-LUN
and boot-chain restore, layout migration, filesystem format/resize adapters,
live firmware/slot/snapshot/ownership gates, cross-boot recovery and a proven
stock-return route remain open. Compression and sparse streams are unfinished.

Linux chroot/OS repair, secure LUKS/BITLK lifecycle, installed Android FBE trust,
Btrfs-capable recovery, boot request consumption/history, managed SSH/SFTP/USB
and Wi-Fi, advanced Android/Windows restore, complete UI/session policy and
optional extensions still require implementation. Parser fuzzing, independent
reproduction, signing, SBOM/licenses and physical acceptance remain open.
No full roadmap phase or broad capability contract is completed here.

See [HOST-RESTORE.md](../docs/HOST-RESTORE.md) and
[COMPREHENSIVE-ROADMAP.md](../docs/COMPREHENSIVE-ROADMAP.md).
