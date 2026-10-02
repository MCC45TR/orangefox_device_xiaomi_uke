# Combined userdata filesystem and GPT checkpoint

Date: 2 October 2026. Source implementation: `7d7c46946bfcb96174d4fe179cb4b63206761ee2`.
Local candidate: `artifacts/ure-partition-job-alpha/`. Nothing was published or
flashed. Earlier sealed candidates were retained.

This checkpoint implements the combined **regular-image** allocation workflow
from the first ordered storage request. It prepares and checks filesystems,
then commits userdata payloads and GPT under one recovery boundary. It does not
complete the whole partition-manager roadmap, open live UFS writes or establish
Android userdata boot compatibility.

## Implemented behavior

The existing keyboard-driven GB/GiB/MiB/percentage designer and native graph now
review `partition.apply-layout`. New ESP/Linux/Windows allocations use only the
original userdata extent. Standard mode preserves its start and identities;
advanced front placement explicitly erases/recreates userdata. Other existing
partition ranges and OEM payloads stay protected. An existing shared ESP is
preserved byte for byte when no new ESP is allocated. Boot registration and
other-partition content formatting require separate workflows.

All original userdata and before/after GPT bytes are captured before any
original write. Preserved userdata is resized in a private working image; new
role filesystems are formatted in separate images. Final capacity, signatures,
available UUIDs and independent read-only checks are verified before application.
Unknown, block-encrypted or fscrypt-enabled userdata is refused. A signature
without a block-encryption marker does not establish FBE access.

The journal seals disjoint payload/GPT ranges and before/after hashes. Payloads
precede backup and primary GPT writes. Durable intent, data synchronization and
readback accompany each chunk. Recovery ignores saved progress, inspects actual
bytes, permits only verified original/desired mixtures, and refuses unrelated
divergence. Full rollback restores the original userdata and GPT. GUI recovery
actions require a bound inspection and exact confirmation.

Buffers are bounded to 64 KiB in the combined range engine. Chunks adapt between
1 and 64 MiB with a bounded count. Reflinks and sparse zero staging avoid
unnecessary copies when supported, but do not reduce the conservative journal
budget of three original userdata sizes plus 64 MiB. Complete image scans occur
at transaction boundaries rather than per chunk. No throughput claim follows
from these optimizations.

## Validation stages

| Stage | Result and boundary |
|---|---|
| Project policy | 19 host checks passed; no hardware test |
| Native C++ | All 18 CTest executables passed with warnings treated as errors |
| Host CLI | Full saved-plan, filesystem, chroot, boot-audit, backup/restore, GPT, stock-GPT, layout, display, installer and forbidden-payload gates passed |
| Real host filesystem fixtures | Actual ext4 high-block file relocation/retention; final FAT32/ext4/NTFS checks; both logical sector sizes; advanced front erase/recreate; existing ESP preservation; complete byte rollback |
| Interruption and refusal | SIGSTOP/SIGKILL during application, forged progress, interrupted before/after bytes, changed target, wrong confirmation, encrypted-feature and unrelated-divergence refusals |
| GUI callbacks | Actual source callbacks against host platform boundaries: selection changes invalidate review, combined application, journal inspection and whole-image rollback; no rendered tablet UI claim |
| Sanitizers | All 18 executables passed ASan/UBSan/leak checks with halt-on-error; vptr instrumentation remains excluded because of the pinned host runtime limitation |
| AArch64 | OrangeFox Android 16 recovery, native CLI/library and GUI compiled with the preserved stock kernel |
| Generic ARM64 VM | Native published CLI/tools, persistent disposable ext4 media, actual guest emergency reboot during application, second-boot inspection/resume and exact full original-image rollback; ten checks passed |
| Final compressed payload | Actual header-v4 LZ4 ramdisk extracted and scanned; 206 ELF objects have dependency closure; two embedded ZIPs pass integrity/privacy/no-Python checks; source/GUI/tools match staging |
| QEMU user fixtures | Extracted CLI file/GPT/stock-GPT/layout/backup/restore/boot-codec fixtures, WIM round trip, filesystem no-action checks and ephemeral SSH key generation passed; no rendering/listener/device |
| Package repeat | Two packaging runs produced identical hashes for all six initially generated assets; this is package repeatability, not a fresh reproducible compilation |
| Physical acceptance | Not run: neither Pad 7 nor POCO Pad X1 boot, live write, UFS forced reboot, Android userdata, rendering/touch or rollback is accepted |

Both host and sanitizer receipts bind input manifest SHA-256
`d5d981a6624fd447c0b44970ba6084529d0e2be37aa4a8490b0864b72d3898e3`.
Sanitizers use pinned Clang executable SHA-256
`55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f`.
The standalone runner reproduces this instrumentation; it adds no tablet runtime.

The VM used QEMU 11.1.2 and generic Linux 7.2.8 `virt`, kernel SHA-256
`5691fe5c19ea69328a3cf2990f2ccb05f7eac1ccb600a4c291e927c95183d8a4`.
Its native CLI is exactly the CLI extracted from the packaged recovery. Neither
that kernel nor its modules are the shipping Uke kernel or an UFS controller.
The VM record explicitly keeps shipping-kernel, tablet, physical-device and UFS
test flags false.

## Failed trial and correction

The first native VM fixture inherited the host e2fsprogs 1.47.4 `orphan_file`
feature. Pinned Android e2fsck 1.46.6 refused `FEATURE_C12` with status 12 during
staging; the original disk image hash stayed unchanged. That trial never
reached its intended reset boundary and is retained as a failure. The corrected
positive fixture explicitly disables that unsupported feature. The successful
runner additionally verifies a separate unsupported-feature refusal, retained
checker diagnostics and an unchanged source image before the reboot scenario.

Initial JsonCpp signed/unsigned round-trip and warning-as-error development
failures were corrected without weakening identity checks or compiler flags.
Saved numeric fields now compare integer values in range; identity checks use
canonical JSON. The full reopen/commit/rollback tests pass with the correction.
The upstream OrangeFox packaging script still emits its existing `local`
outside-function warning; the build completed and final extracted-payload
checks independently passed. These results are recorded separately from device
acceptance in the project engineering lessons.

## Exact artifacts

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Recovery image | 104857600 | `72c2e314941d22044a11542f0196875137aeee52f7d5c023cf317d0f7923f31a` |
| Temporary fastboot-boot image | 100663296 | `3cd009c4aabf0893c338e8016c4a639b73f4d6750a1d653b7f23714a6a8f4f4f` |
| Flashable ZIP | 33650071 | `20d49331e69874f0e018131504c2a8f272de3bfbe5962793acc3a04d6b594666` |
| Compressed recovery ramdisk | 38510491 | `7813af3af9050ba052d20c421bb835de8070c5df5255a33093639d8ccdadd719` |
| Native AArch64 CLI | See payload inventory | `c4afb2221bb7712fa450d3dafe5f47ba617ede379ffd3c4599f98f504c359226` |
| Preserved stock kernel | 35432960 | `97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9` |

The candidate contains original licensed source snapshots and machine-readable
host, sanitizer, extracted-ramdisk and partition-VM receipts. `SHA256SUMS` and
`ARTIFACT-MANIFEST.json` are the final artifact inventory. AVB uses `NONE`; the
ZIP is unsigned. Only Global `OS3.0.303.0.WOZMIXM` is in this build's firmware
scope. CN and Turkey inputs are not interchangeable with it. Follow
`STOCK-RETURN.md`; the temporary-boot image must never be flashed, and the
firmware-matched stock recovery/inactive stock slot remain the fallback.

## Remaining ordered work

1. Partition manager: live firmware/FBE/ownership acceptance, encrypted or
   installed-OS migration, shared ESP boot registration and exact-device forced
   reboot remain open beyond this combined image workflow.
2. Stock return: coordinate all six LUNs and selected verified stock payloads;
   bind separate model/SKU declarations, measured capacities and original-unit
   evidence. Source image length and destination capacity must remain distinct.
3. Live writer: accepted current-kernel ownership, installed firmware/model/SKU,
   exact ranges and real UFS forced-reboot acceptance; current gates stay closed.
4. Filesystems: packaged FAT resizer, exFAT copy/recreate resize and complete
   NTFS repair beyond the limited native `ntfsfix` behavior.
5. Android: installed KeyMint/TEE/FBE trust, A/B/Virtual A/B, super/logical/OTA
   transactions and isolated second Android.
6. Linux rescue: actual Arch/Fedora package/initramfs/SELinux repairs, runtime
   closure, network and GUI cancellation acceptance.
7. Btrfs: firmware-compatible shipping kernel, receive/restore, readable
   subvolume paths and boot integration. Earlier generic-VM Btrfs tests remain
   a separate historical checkpoint.
8. Boot auditing: full executable initramfs closure, actual root-block match,
   Uke DT compatibility, module ABI and trusted signature verification.

No full roadmap phase or physical test is marked complete by this checkpoint.
