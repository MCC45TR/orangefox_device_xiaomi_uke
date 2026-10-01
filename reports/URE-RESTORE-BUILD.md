# Raw image restore and interruption recovery checkpoint

1 October 2026. This advances the supplied 103-topic roadmap for
**Global OS3.0.303.0.WOZMIXM**. The complete roadmap remains unfinished. No tablet,
live UFS write, GUI rendering, electrical power-loss or hardware rollback
acceptance is recorded. The candidate is unsigned and is not a supported release.

## Implementation

The shared C++ engine now plans raw restoration from a complete schema-2 storage
backup. It verifies source chunks and the full hash, matches the selected image
inode/path, geometry, ownership and declared firmware profile, and records a
twice-scanned current-content manifest plus the desired backup identity/hash.
Known disk GUIDs must agree. A changed inode is a different target even when its
bytes match. Malformed hash types and unsupported representations are rejected.
The declared profile remains separate from installed-firmware verification.

Before writing, execution stores immutable original and desired manifests with
private, fsynced and read-back chunk mirrors in the journal. It requires local
space for both raw objects and a 32 MiB margin. Preparation can resume from
verified chunks; after both mirrors are complete, the original external backup
directory is no longer required for recovery. Host-streamed restore that avoids
this local space requirement remains unfinished.

Durable phases distinguish preparation, execution, verification and commit.
Attempted writes and inherited partial target contents record FAILED_UNCERTAIN
on failure. Inspection verifies the persisted plan and both mirrors before
classifying actual bytes as ORIGINAL, TARGET, PARTIAL_EXPECTED_WRITE or DIVERGED.
Resume rechecks current chunk hashes, skips already correct chunks and writes
only from verified target mirrors. Rollback uses verified original mirrors and
preserves its direction after interruption. Both operations fsync/read back each
chunk and verify the full result. Safe cancellation requires the original bytes
and a pre-execution phase. Detailed inspection output is bounded to 128 chunk rows.

CLI and native OrangeFox pages share the engine. The UI exposes image/source
selection, estimated journal space, plan confirmation and inspected recovery
actions. Its default `/tmp` journal parent is volatile; persistent rollback
requires a verified persistent destination. GPT-image operations now share the
same write gate, which refuses physical block writes, live loop attachments and
unavailable loop-ownership evidence. Locks serialize cooperating transactions;
they do not exclude hostile writers or establish an atomic filesystem snapshot.

## Evidence

| Class | Result and limits |
|---|---|
| Root host suite | 19 checks pass; source/archive, policy, privacy and dependency order only |
| Native C++ | Five CTest executables pass; new raw-restore cases cover 512/4096 geometry, identity/profile/confirmation refusal, locks, mirrors, metadata, actual preparation/write/rollback SIGKILL, partial-byte continuation, EFBIG uncertainty/resume and safe cancellation |
| CLI | Persisted JSON plan, malformed hash types, wrong profile/target/context/sector, exact confirmation, full readback, source-independent journal completion and rollback pass; existing storage/file/GPT and installer fixtures also pass |
| Sanitizers | Pinned Android-host Clang ASan/UBSan with leak detection passes all five executables and raw-restore CLI fixtures; vptr is excluded because the bundled runtime lacks its handlers |
| Android build | Locked OrangeFox Android 16 recoveryimage completes in 2:23; shared engine, JSON CLI and native restore pages compile for AArch64 |
| Final compressed ramdisk | Actual header-v4 LZ4 payload passes privacy/no-Python, two recursive embedded ZIP scans, GUI XML/tool-manifest comparison and all 205 ELF architecture/interpreter/dependency checks |
| AArch64 QEMU | New 512/4096 raw-image plan/execute/inspect/readback-resume/rollback and refusal fixtures pass; existing file/GPT journals, storage backup/receiver, WIM round trip, ext4/exFAT/NTFS no-action and ephemeral SSH-key fixtures also pass |
| Package | Dedicated/temporary image roles, stock kernel and ZIP checks pass; two package runs from the same built image have identical hashes; independent binary reproducibility remains open |
| Hardware | UNTESTED; process/file fixtures are not tablet or electrical power-loss evidence |

## Artifact identity

The new local candidate is `artifacts/ure-restore-alpha/`. All five earlier
candidate directories retain passing original SHA256SUMS.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Dedicated recovery IMG | 104857600 | `bcdab4d09d14270184ab4610ca208fb73a744f22ede8b3ecefb1276284f81226` |
| Temporary-boot IMG | 100663296 | `23ab0652e37238f5839da580801b782a9fcd4092bba03ff60ed1e74b810eecec` |
| Active-slot installer ZIP | 32416585 | `1e41176f9b84aba925004121a5deb20e5d725c354c04a77cbf0a3635e2e49232` |

The compressed ramdisk contains 37158598 bytes with SHA-256
`2d22a53f32a3362057ee56d2b0705cb3b7d455ddb1aca242990709f28c7dc5d1`.
Its CLI has SHA-256
`3de401d9cd6e10bd10c04c6625fca91da8aa6d45ef5ab19fd709bc8c0588c98e`.
The unmodified stock kernel remains
`97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9`.
AVB is NONE and the ZIP is unsigned. The temporary-boot IMG must never be flashed.
Independent binary reproduction and physical compatibility are not established.

## Remaining scope

Real block writes, firmware/slot/snapshot integration, positive live ownership
acceptance, cross-boot identity and host-streamed restore remain open. Local
restore currently stores both raw objects; compression and sparse stream formats
are not implemented. Advisory locks and repeated observations do not exclude
all concurrent writers. Electrical power-loss acceptance requires hardware.

Multi-LUN/boot-chain packages, layout migration, filesystem formatting/resizing,
controlled chroot and OS repair, secure LUKS/BITLK lifecycle, a Btrfs-capable
recovery kernel, boot request consumption/history, managed SSH/SFTP/USB/Wi-Fi,
Windows restore, signed updates, full UI/session policy and optional contracts
still require implementation. No full phase or contract is completed here.

See [URE-NATIVE.md](../docs/URE-NATIVE.md) for the implemented interfaces and
[COMPREHENSIVE-ROADMAP.md](../docs/COMPREHENSIVE-ROADMAP.md) for all 103 topics.
