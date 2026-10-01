# Journal recovery and streaming backup checkpoint

1 October 2026. This continues the supplied 103-topic recovery roadmap for
**Global OS3.0.303.0.WOZMIXM**. The whole roadmap remains open. No tablet has been
booted, flashed, connected through SSH/ADB or used to validate these additions.

## Implementation added

File transactions now persist the sealed plan before any target write. Private
journals retain directory descriptors, use exclusive creation and share a
nonblocking mutation lock. Read-only inspection and discovery distinguish exact
unchanged source identity, verified payload, restored original content and
unrelated divergence. Confirmed resume can execute an unchanged backed-up plan,
or finish a commit record from readback without rewriting already verified data.
Cancellation requires the original identity to remain unchanged. Wrong roots,
corrupt backups, unsupported recorded phases and divergent targets refuse
recovery. Older journals retain their existing rollback path.

Large regular-file and image backups now use a 64 KiB buffer, configurable
64 KiB–64 MiB chunks, per-chunk hashes and final SHA-256. The manifest seals the
root/file identity, source size, declared profile and transfer geometry. Capture
verifies planned data and readback, fsyncs and publishes chunks without replacing
an existing chunk. Resume verifies published data instead of trusting stale
progress. Binary export sends one chunk to stdout and errors to stderr.

The host-only Bash receiver supports a local CLI, ADB exec-out and an existing
SSH endpoint. It validates byte counts and hashes before exclusive publication,
resumes at verified boundaries, uses strict SSH host-key checking and quotes
remote arguments. It does not configure networking, start services or import
keys. Host transport testing uses a mock remote shell rather than a tablet.

The native GUI now exposes journal discovery/inspection, reviewed resume/cancel/
rollback, an explicit journal parent, backup planning, local capture, stored-data
verification and resume. The default journal parent `/tmp` is volatile; users
must select a persistent destination before depending on a reboot recovery
record. GUI XML and compilation are separate from rendering and touch acceptance.

## Evidence

| Class | Result and limits |
|---|---|
| Root host suite | All 19 source, archive, policy, privacy and dependency-order checks pass |
| Native host C++ | Two CTest executables pass; covers persisted-phase recovery, readback-only commit, cancellation, locks, divergence, corrupt backups, chunk/full hashes, wrong roots, empty sources and an actual SIGKILL followed by verified resume |
| CLI and host receiver | Large-file capture, binary framing and stderr errors, missing-tail resume, local receiver, reconnect and mocked SSH quoting with spaces/apostrophes/literal shell syntax pass |
| Existing checks | Legacy recovery identity, installer slot/snapshot/stock-hash/refusal and forbidden/nested payload fixtures pass |
| Sanitizers | Pinned Android-host Clang ASan/UBSan and leak detection pass both CTest executables; vptr remains excluded because the bundled runtime lacks its handlers |
| Android build | Locked OrangeFox Android 16 `recoveryimage` completes in 3:41; shared library, native GUI and CLI compile for AArch64 |
| Extracted ramdisk | Final header-v4 LZ4 payload passes privacy/no-Python, both embedded ZIP scans, GUI XML/tool-manifest comparison and all 205 ELF architecture/interpreter/dependency checks |
| AArch64 QEMU | Inspected journals and readback-only recovery, streamed capture/export/resume and local/mocked host receivers pass; legacy identity, WIM round trip, ext4/exFAT/NTFS no-action checks and ephemeral key generation also pass |
| Package | Dedicated/temporary image roles, stock kernel, shared ramdisk and installer ZIP are validated separately; two packaging runs from the same built image have identical hashes |
| Hardware | UNTESTED; process interruption is not electrical power-loss evidence |

## Artifact identity

The new local directory is `artifacts/ure-streaming-alpha/`. The earlier
`artifacts/ure-native-alpha/` and `artifacts/prerelease/` remain separate.
Neither a network rescue session nor a device storage write has been run.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Dedicated recovery IMG | 104857600 | `b405485d24e2ab180494fda43e074afb2727197aa2ceaca3fa449eb8b07d24bc` |
| Temporary-boot IMG | 100663296 | `e43846f20773cd7d8e6e39b922cac25ee7f0f18af978a89ffa07e8b4d4696ac5` |
| Active-slot installer ZIP | 32120925 | `8d8d66ff2db46a6d31e442514f0dfa4ecfc595fd7cd7048c6b77396eedb2b73c` |

The compressed ramdisk contains 36834069 bytes with SHA-256
`7d97d77afdbdaadbb27d5a1833bd01a6ebf175b29cd9f901e839342f477939d8`.
Its CLI SHA-256 is
`7e1cbce04a160a205904fad427de8fa0dbbd4497a28abbde78bbb9e92a5be665`.
The unmodified stock kernel remains
`97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9`.
AVB is NONE and the ZIP is unsigned. The temporary-boot IMG must never be flashed.
Exact native/test input hashes and source snapshots accompany the manifest;
they do not claim a full independently reproducible Android rebuild.

## Remaining scope

The new stream format covers regular files and file-backed images. Stable
cross-boot live partition capture, application-consistent snapshots, sparse
encoding/compression and managed restore transactions still require source work.
The declared profile in a file manifest is not installed-firmware verification.
Advisory locks do not stop an unrelated or hostile writer. Plans and backups
are private records; the public diagnostic exporter does not include them.

General block transactions, GPT backup/repair/layout design, semantic OS/config
repair, controlled chroot, cryptsetup/secure unlock, a Btrfs-capable recovery
kernel/userspace, boot backend/history, managed SSH/SFTP/gadget ownership, Wi-Fi,
Windows restore, signed updates, full UI/session policy and the remaining
optional contracts stay in the roadmap. No complete phase or contract is marked
finished by this checkpoint.

See [URE-NATIVE.md](../docs/URE-NATIVE.md) for the concrete interfaces and
[COMPREHENSIVE-ROADMAP.md](../docs/COMPREHENSIVE-ROADMAP.md) for all topics.
