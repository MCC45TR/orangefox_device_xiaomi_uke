# Reviewed boot requests: source and build checkpoint

Date: 3 October 2026. Implementation commit: `f745820`; active-scope correction:
`1e41537`. This checkpoint adds the native boot-manager lifecycle and OrangeFox
review/history pages. It preserves the existing default and does not enable real
EFI variable writes, loader execution, reboot or slot changes.

The [routing contract](../docs/BOOT-ROUTING.md) defines the supported EFI parser,
exact loader/context binding, request ownership, bounded history, correlated
synthetic acknowledgement and request retirement. A consumed request without a
receipt is unknown; disappearance of BootNext is not an OS failure. Cancelled or
completed request IDs cannot be reused through another journal. Retirement is
published atomically before ownership release. Private fixture records are not
signed or protected from an administrator rolling back the entire store.

## Validation before publication

| Stage | Result and limit |
|---|---|
| Root policy | All 19 project host checks passed; no hardware test |
| Native C++ | All 23 CTest executables passed with warnings as errors in 215.67 seconds |
| Actual callback coverage | Boot inventory, exact review invalidation, stage visibility, journal actions and keyboard field policy run through the real OrangeFox callback code with host UI stand-ins |
| Interruption coverage | Actual child SIGKILL at ARMING, CONSUMING and retirement/owner-release boundaries; unknown outcome, ownership and cross-journal replay checks pass |
| CLI and existing features | Complete host gates passed, including backup/restore, tree metadata, GPT/stock jobs, partition layouts and filesystem transactions, display settings, isolated chroot cleanup, codecs, installer and forbidden payload fixtures |
| Sanitizers | All 23 CTests passed pinned address/undefined/leak checks in 462.05 seconds; the previously documented vptr runtime exclusion remains |
| ARM64 recovery | Native CLI/library, installer and OrangeFox compiled with the preserved stock kernel in 4:23 |
| Optional VM fixture | Separately rebuilt the uninstalled native Btrfs fixture; it is excluded from the shipping ramdisk |

Native/sanitizer input-manifest SHA-256:
`df6ca8a2b298063ae59c0493af9db87192ed4201ef5eaa47dd53acfccdb07f63`.
Host CLI SHA-256:
`d157d0d8d6078d136145259190912a68f5e2f6a4791d23ce106d5507567cc237`.
The pinned instrumentation compiler SHA-256 remains
`55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f`.

Failed prototype trials are retained in the root
[REC-B001–REC-B004 lessons](../../docs/lessons/2026-10-03-RECOVERY-BOOT-ROUTER.md).
They include cross-journal replay, torn record publication, JSON identity round
trips and strict compiler fixture corrections. Raw logs remain private.

## Publication and VM boundaries

The candidate is `artifacts/ure-boot-router-alpha/`. Its extracted-payload audit,
checksums and sealed source manifest are recorded separately after packaging.
Earlier candidates retain their original bytes and receipts. No image is
flashed, no host block device is attached to a VM, and no tablet result is
created by this checkpoint.

The requested next stage is a fresh isolated ARM64 system-VM review using the
exact current CLI and uninstalled Btrfs fixture, plus an actual OrangeFox display
trial where the generic virtual hardware permits it. Generic VM kernel/module
evidence must remain distinct from the preserved stock recovery kernel. Prior
guest-reset evidence belongs to its earlier binary and is not copied here.

The ordered work streams remain boot routing; LUKS/Windows; ADB/backups;
files/sessions; distribution/diagnostics. BitLocker and network/SSH/SFTP rescue
are deferred by the owner. Existing dormant third-party tooling is not evidence
of a managed service. Real Uke/Aloha routing, trusted OS health, accepted
LUKS/Windows workflows, broader ADB sessions, package-repair acceptance, signed
updates and physical display/input/storage validation remain open.
