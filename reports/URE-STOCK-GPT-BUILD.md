# Native stock GPT reconstruction checkpoint

1 October 2026. Scope: **Global OS3.0.303.0.WOZMIXM**, all six UFS LUNs,
and isolated image metadata transactions. This is a local unsigned experimental
candidate. GitHub upload is deferred while the complete original roadmap and
expanded partition-manager implementation take priority. No tablet, physical
UFS write, rendered UI, electrical power-loss or hardware stock-return result
is recorded. The comprehensive partition manager remains unfinished.

## Implemented behavior

The native C++ stock engine verifies five exact OEM GPT/rawprogram/patch hashes
for the selected LUN, derives terminal ranges and backup metadata from selected
capacity, recomputes table/header CRCs and validates both copies on a sparse
metadata reconstruction. It applies the reviewed semantics of pinned patch
files; it never executes donor/OEM scripts or interprets arbitrary expressions.
The protective MBR count also follows selected capacity instead of the OEM
template's unconditional saturation. Buffers contain metadata only, independent
of the full object size. No measured tablet throughput is claimed.

A private preview explicitly carries OEM template identities and cannot serve
as an original-unit backup or executable restore plan. A stock plan instead
preserves disk and partition GUIDs from the selected target's healthy GPT or a
verified same-target, capacity/sector/profile-matched original GPT backup.
Missing identities are refused. Unknown extra partitions stay protected even
when an older backup supplies stock identities. Reviewed changes expose before
and after ranges, removed OS entries and metadata-only effects on visibility.

LUNs 1–5 contain an OEM `last_parti` reservation with zero type GUID, nonzero
unique GUID, vendor attribute bit 60 and a terminal range. Inspection reports
it separately from real partitions, checks its overlaps and preserves it.
Other nonzero unused entries remain invalid. Older schema-1 records without an
empty reservation inventory remain verifiable; a nonempty inventory cannot be
silently discarded. Source reasoning is documented in
[PARTITION-MANAGER.md](../docs/PARTITION-MANAGER.md).

Stock metadata plans use the shared GPT confirmation, revalidation, durable
original/desired journal, ordered backup-first writes, readback, inspection,
readback-only commit and rollback engine. Inputs are reverified and output is
regenerated before execution. The CLI and native review pages expose this
workflow. Physical writes remain refused by the common storage gate. Metadata
restoration does not restore partition contents, move data, resize a filesystem
or prove Android/calibration/key recovery. The default `/tmp` journal is volatile.

## Evidence

| Class | Result and limits |
|---|---|
| Root host suite | 19 source/archive, privacy, policy and dependency-order checks pass |
| Native C++ | Seven CTest executables pass; new stock cases compare all six LUNs at two capacities against an independent XML patch oracle, preserve original GUIDs distinct from OEM templates, verify execution/readback/rollback, original-backup recovery, simulated partial metadata recovery and refusal cases |
| CLI | Six sparse LUN images pass preview, original-backup identity recovery, exact confirmation, metadata commit, current-byte inspection and rollback; profile/capacity/LUN/context and OEM hash refusals pass |
| Sanitizers | ASan/UBSan with leak detection passes all seven executables and stock CLI; the final older-schema compatibility test also passes a focused rerun; vptr is excluded because the pinned runtime lacks its handlers |
| Android build | Locked OrangeFox Android 16 recoveryimage completes in 3:20; stock engine, CLI and native stock input/identity/review pages compile for AArch64 |
| Final compressed ramdisk | Actual header-v4 LZ4 payload passes privacy/no-Python, two recursive embedded ZIP scans, XML/tool manifest, staged-binary matching and ELF dependency closure |
| AArch64 QEMU | Six-LUN stock reconstruction and metadata recovery pass from extracted target binaries, alongside file/GPT/raw restore, host-stream transport mocks, WIM, filesystem no-action and ephemeral SSH-key fixtures; no mounts, tablet or real network service |
| Package | Two runs from the same built image have identical hashes; dedicated/temporary roles, stock kernel and ZIP payload agree; independent binary reproduction is open |
| Hardware | UNTESTED; sparse images and QEMU are not physical stock-return or electrical power-loss evidence |

The XML oracle uses host libxml2. Neither libuke nor the tablet CLI acquires that
dependency. No project-owned or tablet Python is introduced.

## Artifact identity

Local candidate: `artifacts/ure-stock-gpt-alpha/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Dedicated recovery IMG | 104857600 | `a5503f2dc811ef1ded07693b22bd4bb404ace975e7668a3353001b8b9acedeea` |
| Temporary-boot IMG | 100663296 | `8a51dff366caae2abdc4140e41af13c150e73b0c704e0812cf6abdb94826528d` |
| Active-slot installer ZIP | 32527779 | `20424f1d61bbcae588103dcf056782db0b3c2061038a6de33d9e829d4945a642` |

Compressed ramdisk: 37274915 bytes, SHA-256
`d5550fb6709cb4a906e6f88d3e941b050cfa668f507550ac13f7717ad08f86bf`.
Extracted AArch64 CLI: SHA-256
`ebda6fe62109511dd9b58ecfb2a2fae64985c563d5d4cbbbca636dae70e2ad85`.
The stock kernel remains
`97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9`.
AVB is NONE and the ZIP is unsigned. The temporary-boot IMG must never be flashed.

## Remaining scope

Full create/delete/rename/type/attribute/resize/move management, ownership-aware
filesystem/data migration, multi-LUN durable orchestration, firmware-matched
partition contents and boot-chain restore, installed firmware/slot/snapshot
gates, cross-boot identity and physical stock return remain open. Userdata size
is capacity-derived; its filesystem or encryption is not recreated by GPT work.

Linux chroot/OS repair, LUKS/BITLK lifecycle and installed Android FBE trust,
Btrfs-capable recovery, consumed boot requests/history, managed USB/SSH/SFTP and
Wi-Fi, advanced Android/Windows restore, complete UI/session policy, optional
extensions, parser fuzzing, independent reproduction, signing, SBOM/licenses and
physical acceptance remain separate unfinished roadmap requirements. No full
phase or broad capability contract is marked complete.
