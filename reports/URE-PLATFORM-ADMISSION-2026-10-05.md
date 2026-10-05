# Platform admission checkpoint — 5 October 2026

AUD-034 remains open. This checkpoint implements eleven explicit capability
contracts, bounded read-only observations and refusal before unaccepted device
actions. It does not implement the missing physical Android, Linux or EFI
backends. The [admission guide](../docs/PLATFORM-ADMISSION.md) records each
requirement and the declaration-only comparison boundary.

## Source and focused validation

The existing Extra-menu capability callback and JSON CLI expose FBE, slots,
Virtual A/B, super, OTA, second Android, one-shot boot, fallback, kernel/boot
coherence, installed Linux repair and storage writing separately. All retain
exact-unit, backend and health blockers. Read-only Android observations use at
most twelve bootctl queries with a two-second deadline each. Repeated equal
queries do not claim an atomic snapshot. Standard battery/thermal/UFS readings
retain absent, denied and malformed states; no calibration or threshold policy
is inferred from another device.

Five focused native CTest entries passed in 26.55 seconds. The same five entries
passed with pinned Clang address/undefined/leak instrumentation in 53.45 seconds.
Actual CLI controls compare declaration context and every prerequisite digest,
reject foreign or stale observations, and refuse unavailable operations before
opening a FIFO request, root, target or journal. The Android-compiled host ABI
test checks the twelve-query allowlist, changed reads, cached idle text after
timeout and all eleven write refusals. It is a host fixture; its tool wrapper is
not compiled into the shipping Android module.

Four current translation units (`platform`, `management`, `preflight`,
`diagnostics`) compiled against pinned Bionic headers for
`aarch64-linux-android10000`. Each repeated object matched byte for byte; the
exact pre-discovered and compiled header sets remained unchanged. The receipt
is `platform-arm64-evidence-GpV72XgI/verification.json`. This proves focused
target compilation, not a linked recovery image, complete Android build,
extracted payload, combined guest or tablet boot. Later aggregate input changes
require a matching fresh receipt before release acceptance.

## Preserved failed trials and corrections

Initial focused trials exposed a missing fixture include directory, a JSON
comparison using an unsupported C++ operator, and a CLI oracle comparing
timestamped envelopes instead of their data. These were corrected without
weakening admission. Clang rejected the host wrapper's C-linkage declaration
returning a C++ object; a C++ declaration with an explicit linker symbol fixed
the fixture. The first header-inventory pass incorrectly excluded `+` in a
libc++ path; the path grammar was corrected and a fresh compile/repeat receipt
was collected. Failed trial directories remain private and do not count as
passes.

The first complete 53-test native attempt also found an older installer test
assuming that `/tmp` was RAM-backed. The host reserve policy now binds disk
scratch there. The negative control was changed to verify the actual tmpfs
filesystem at `/dev/shm` before testing RAM-journal refusal. The production
durability and device-write policies were not relaxed.

## Device and release boundary

The owner reports POCO Pad X1 with OS3.0.303.0.WOZMIXM as the intended test
configuration. This is a current user report, not a privileged measurement of
installed firmware bytes, geometry, bootloader state or fallback. The older
OS2 inventory is retained as historical evidence. Global OS3 stock source
inputs do not independently accept this physical unit.

The production standalone installer's `install` mode currently refuses with
`installer-durability-unavailable` before image or block access. The common
OrangeFox package-install gate also refuses live installation. Consequently a
ZIP containing those binaries cannot be described as a working sideload
installer. A three-asset installable release requires a complete current build,
image/payload/capacity checks and an accepted dedicated installer path; changing
a release label or enabling a boolean does not supply these requirements.

No tablet was flashed, rebooted, mounted or calibrated for this checkpoint.

Experimental Alpha 1 was removed from GitHub as requested after all thirteen
published assets were matched against their archived sizes and SHA-256 digests.
The source tag and verified local archive were preserved. No replacement
installable release was published merely to satisfy an asset count.
