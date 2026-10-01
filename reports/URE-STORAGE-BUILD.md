# Storage usage and raw backup checkpoint

1 October 2026. This advances the supplied 103-topic roadmap for
**Global OS3.0.303.0.WOZMIXM**. The full roadmap remains unfinished. No tablet,
live UFS read/write, GUI rendering, electrical power-loss or hardware rollback
acceptance is recorded. The candidate is unsigned and is not a supported release.

## Implementation

The shared native library now collects bounded storage ownership observations:
mounts in visible process namespaces, writable descriptors with inode/flags
revalidation, swap backing and configfs mass-storage backing. Parent LUN and
dependent mapper relations propagate ownership; independent sibling mounts do
not block a selected partition. Missing evidence, restricted visibility,
unknown dependencies, cycles/depth and unresolved legacy USB ownership refuse
a quiescent classification. Real proc/configfs checks prevent regular fixture
directories from masquerading as complete kernel evidence.

Schema-2 storage backup plans bind a source to geometry, the selected image inode
or live unit/LUN/partition identity, current boot, declared firmware profile and
chunk/full hashes. Existing schema-1 regular-file plans remain supported. Live
source selection retains an acquired read-only kernel block claim; acquisition
is recorded directly because the inspected stock kernel clears O_EXCL after
opening. Revalidation uses the retained descriptor and compares current identity.
Planning scans storage twice. Capture rechecks source/usage around each chunk,
fsyncs, reads back and exclusively publishes chunks, then rereads the complete
source before success. The final exported chunk also requires full current-source
verification. Live destinations on the source or related mapper are rejected.

CLI, native GUI and host receiver share the implementation. The GUI reviews the
source, geometry and destination before capture, uses 64 MiB storage chunks,
and exposes explicit resume/verify and usage inspection. Storage plans use their
sealed source and optional system context; file plans require a selected root.
Binary stdout and JSON stderr remain separate. The host receiver supports local,
ADB or an existing strictly authenticated SSH transport without starting a service.

These checks bind observed bytes to a plan; they do not establish an atomic
filesystem/application snapshot or globally exclude uncooperative raw writers.
Profiles are declarations, not installed-firmware trust. Live positive acceptance
and cross-boot continuation remain open; no backup manifest authorizes restore.
All real block writes remain gated by the unfinished common firmware/slot/snapshot
and ownership integration.

## Evidence

| Class | Result and limits |
|---|---|
| Root host suite | 19 checks pass; source/archive, policy, privacy and dependency order only |
| Native C++ | Four CTest executables pass, including parent/mapper/sibling policy, namespace mounts, writers, swap/USB, unknown evidence, cycles, bounded output and ambiguous identity refusal |
| CLI | 512/4096-byte image plans, capture/resume/export/receiver, chunk/full hashes, inode replacement, offline verification, context/unit and non-regular source refusal pass; existing file/GPT journals, transport quoting and installer fixtures pass |
| Sanitizers | Pinned Android-host Clang ASan/UBSan with leak detection passes all four executables and new storage CLI fixtures; vptr is excluded because the bundled runtime lacks its handlers |
| Android build | Locked OrangeFox Android 16 recoveryimage completes in 2:08 after resolving the missing Bionic configfs constant; shared library, JSON CLI and native GUI compile for AArch64 |
| Final compressed ramdisk | Actual header-v4 LZ4 payload passes privacy/no-Python, two recursive embedded ZIP scans, GUI XML/tool-manifest comparison and all 205 ELF architecture/interpreter/dependency checks |
| AArch64 QEMU | New 512/4096 storage-image plan/capture/resume/export/receiver and identity/context/unit refusal fixtures pass; previous file/GPT journals, streamed receivers, WIM round trip, ext4/exFAT/NTFS no-action and ephemeral SSH-key fixtures also pass |
| Package | Dedicated/temporary image roles, stock kernel and ZIP checks pass; two package runs from the same built image have identical hashes; independent binary reproducibility remains open |
| Hardware | UNTESTED; synthetic observations/images and process interruption are not tablet or electrical power-loss evidence |

## Artifact identity

The new local candidate destination is `artifacts/ure-storage-alpha/`. Existing
`artifacts/ure-gpt-alpha/`, `artifacts/ure-streaming-alpha/`,
`artifacts/ure-native-alpha/` and `artifacts/prerelease/` remain separate.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Dedicated recovery IMG | 104857600 | `cef6b445c79134439863128847b00dea5925c82fbeaebd58249e458a554396e4` |
| Temporary-boot IMG | 100663296 | `c95d8d14b75a326264e3103588cb79b18366cac7af97d0cd158af9dbf9ca0955` |
| Active-slot installer ZIP | 32341864 | `ca4c22e2562e7d7b10351a0502b607e6353e59ceffd27bebd1da686744700130` |

The compressed ramdisk contains 37072987 bytes with SHA-256
`e9a681b171cb2fc89033768750bc7f4e224e46e7ebc13dd35e95b7b6bb45dc08`.
Its CLI contains 749816 bytes with SHA-256
`ee2b9c49a00212478ab1c9c66d196d0bd16177b7bbfed3b5b1497bbac3bf916b`.
The unmodified stock kernel remains
`97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9`.
AVB is NONE and the ZIP is unsigned. The temporary-boot IMG must never be flashed.
Independent binary reproducibility and physical compatibility are not established.

## Remaining scope

Positive live block-claim/ownership/transfer tests, cross-boot manifests, restore
transactions, multi-LUN/slot boot-chain packages, sparse/compressed streams and
electrical power-loss acceptance remain open. Mount/write/gadget observations
can change between checks and do not exclude every kernel or hostile writer.

Layout migration, filesystem formatting/resizing, controlled chroot and OS repair,
cryptsetup/secure unlock, Btrfs-capable recovery kernel/userspace, boot request
consumption/history, managed SSH/SFTP/USB/Wi-Fi, Windows restore, signed updates,
UI/session policy and optional contracts still require implementation. No full
phase or contract is completed by this checkpoint.

See [URE-NATIVE.md](../docs/URE-NATIVE.md) for implemented interfaces and
[COMPREHENSIVE-ROADMAP.md](../docs/COMPREHENSIVE-ROADMAP.md) for all 103 topics.
