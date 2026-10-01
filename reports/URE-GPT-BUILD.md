# GPT backup and image repair checkpoint

1 October 2026. This advances the supplied 103-topic roadmap for
**Global OS3.0.303.0.WOZMIXM**. The full roadmap remains unfinished. No tablet,
live UFS write, GUI rendering, electrical power-loss or hardware rollback result
is recorded here. The candidate is unsigned and is not a supported release.

## Implementation

The shared native library now selects read-only live storage by stable identity,
checks block device numbers, capacity and sector ioctls, and compares partitions
with the parent GPT. Private unit/LUN evidence is retained when available;
positive hardware identity/ownership acceptance remains open.

GPT parsing independently validates both copies, reciprocal positions, CRCs,
reserved entry-array space, usable-area boundaries, GUID/range conflicts,
protective MBR and UTF-16 labels. Private backups contain five raw metadata
ranges, a partition map, identity/profile manifest and SHA256SUMS. Verification
reconstructs the actual saved GPT and rejects forged non-GPT geometry even when
the manifest has a self-consistent seal.

Repair plans accept a single valid copy. Restore plans require a healthy backup
for the same image or verified unit/LUN, capacity, sector size and declared
profile. Metadata destinations inside current usable space require a separate
layout/migration transaction. Plans seal original/desired tables and hashes.
Regular-image execution checks loop ownership, revalidates, persists original
and target metadata before EXECUTING, writes backup before primary, and fsyncs
and verifies each range and the final table. All real block writes remain gated
pending common firmware/slot/snapshot and storage-ownership integration.

Private journal inspection verifies original/target backups, unchanged protected
metadata and current bytes, distinguishing original, target, expected partial
write and unrelated divergence. Rollback restores only sealed metadata on the
same target and refuses divergence. Readback resume only finishes a recorded
commit when the target table already matches; it never replays interrupted writes.
New GUI pages expose these operations and explicitly hide live execution.

## Evidence

| Class | Result and limits |
|---|---|
| Root host suite | 19 checks pass; source/archive, policy, privacy and dependency order only |
| Native C++ | Three CTest executables pass; GPT covers 512/4096-byte sectors, both-copy repair, actual-byte backup validation, ordered restore, usable-area protection, partial rollback, divergence, journal locks and readback-only commit |
| CLI | GPT backup/verify/compare, target/profile/confirmation refusal, image repair/restore/rollback and inspected readback resume pass; previous editor/journal/streaming/receiver and installer fixtures also pass |
| Sanitizers | Pinned Android-host Clang ASan/UBSan with leak detection passes all three executables; vptr is excluded because the bundled runtime lacks its handlers |
| Android build | Final locked OrangeFox Android 16 `recoveryimage` completes in 2:11; the shared library, JSON CLI and new native GUI actions compile for AArch64 |
| Final compressed ramdisk | Actual header-v4 LZ4 payload passes privacy/no-Python, both embedded ZIP scans, GUI XML/tool-manifest comparison and all 205 ELF architecture/interpreter/dependency checks |
| AArch64 QEMU | GPT backup/verify/compare, image repair/restore/rollback, inspected readback resume and non-regular target refusal pass; previous file/journal/stream/receiver, WIM round trip, ext4/exFAT/NTFS no-action and ephemeral SSH key fixtures also pass |
| Package | Dedicated/temporary image roles, stock kernel and ZIP checks pass; two packaging runs from the same built image have identical hashes; independent binary reproducibility remains open |
| Hardware | UNTESTED; synthetic images and process interruption are not tablet or electrical power-loss evidence |

## Artifact identity

The local candidate destination is `artifacts/ure-gpt-alpha/`. Earlier
`artifacts/ure-streaming-alpha/`, `artifacts/ure-native-alpha/` and
`artifacts/prerelease/` remain separate.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Dedicated recovery IMG | 104857600 | `67588f3b8439501aec0cd490d6fa0447486a13d6caa08593bf5f08bb7ed95c12` |
| Temporary-boot IMG | 100663296 | `c1227dacaa39efbb6a96c3049eb9670edc7da501f11fc5a6c95e371b06912d1b` |
| Active-slot installer ZIP | 32269321 | `89d5e625fd59f87e4ef4ba955b8bdc2aee3c3b25ecfd95286b8d7819ca59069a` |

The compressed ramdisk contains 36993008 bytes with SHA-256
`a015c1e8fd71db39eb2d6151c25ec956a3f306bd5742ca947fa1bb7e0d202424`.
Its CLI contains 683728 bytes with SHA-256
`b64b13656ad7a662820c89c40d082dd901958aac51449dc6e35d2eeba07ecd2b`.
The unmodified stock kernel remains
`97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9`.
AVB is NONE and the ZIP is unsigned. The temporary-boot IMG must never be flashed.
Independent binary reproducibility and physical compatibility are not established.

## Remaining scope

Live GPT writes, cross-boot unit identity/ownership, multi-LUN packages,
stock boot-chain backups, layout migration, filesystem formatting/resizing,
live partition streaming/restores and electrical power-loss acceptance remain
open. Profiles in image manifests are declarations, not installed-firmware trust.
Advisory locks do not stop an unrelated or hostile writer. Incomplete GPT backups
cannot use the new journal recovery path.

Controlled chroot and OS repair, cryptsetup/secure unlock, a Btrfs-capable recovery
kernel/userspace, boot request consumption/history, managed SSH/SFTP/USB/Wi-Fi,
Windows restore, signed updates, UI/session policy and optional contracts still
require implementation. No full phase or contract is completed by this checkpoint.

See [URE-NATIVE.md](../docs/URE-NATIVE.md) for concrete interfaces and
[COMPREHENSIVE-ROADMAP.md](../docs/COMPREHENSIVE-ROADMAP.md) for all 103 topics.
