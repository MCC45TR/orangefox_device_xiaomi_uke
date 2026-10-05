# Android, boot and device admission

The native platform report separates eleven operation families and identifies
their exact missing trust, runtime, ownership, fallback and health requirements.
It is available in **Extra → Diagnostics and recovery → Available capabilities**,
through the existing OrangeFox capability callback, and through ADB:

```sh
adb shell uke-recoveryctl platform capabilities --profile EXACT_PROFILE --json
adb shell uke-recoveryctl android preflight --profile EXACT_PROFILE --json
```

An explicit profile is a requested comparison scope. It is never proof of the
installed commercial model, SKU, firmware, unit geometry or trusted boot chain.
Both Pad 7 and POCO Pad X1 require independently accepted unit evidence. The
Global stock source pins and OS2 stock inventory do not supply that acceptance.

All eleven device backends remain unavailable. This change implements admission
contracts, bounded read-only observations and verified early refusal. It does
not implement an accepted KeyMint/TEE client, Android updater, Uke Aloha port,
physical writer or successful installed Linux repair. A complete host comparison
is explicitly `fixture_declarations_only`, with `live_action_allowed: false`.

## Feature requirements

| Capability | Required evidence beyond exact unit identity and health |
|---|---|
| `android-fbe` | Installed AVB policy, KeyMint/TEE chain, metadata-key origin, separate user keys and fscrypt/kernel/service closure |
| `android-slots` | Both whole boot stacks, boot-control HAL closure, persistent owner, forced-restart recovery and retained stock return |
| `android-snapshots` | Snapshot metadata/COW closure, snapuserd/dm-user, boot-control HAL, persistent owner and forced-restart recovery |
| `android-super` | Both super metadata copies, bounded extents/groups, snapshot ownership and interrupted-operation recovery |
| `android-ota` | Authenticated payload, version/rollback policy, AVB and whole boot stacks, super/snapshot/runtime closure and stock fallback |
| `android-secondary` | Independent Android data/key and metadata/misc ownership, exact boot/AVB/TEE policy, persistent owner and stock fallback |
| `boot-once` | Unit-specific UEFI/Aloha, exact variable-store/ESP identity, loader signature policy and root/DT/module chain |
| `boot-fallback` | The boot-once requirements plus a trusted OS health receipt and preserved stock return |
| `kernel-boot-chain` | Actual root block identity, DT/module ABI, every required initramfs executable and loader signature policy |
| `linux-repair` | Selected installed distro/runtime, independently checked package/initramfs/SELinux results and durable operation recovery |
| `storage-write` | Accepted physical writer, persistent ownership, forced-restart recovery and stock return |

Every feature retains separate `exact-unit-profile-unaccepted`,
`platform-backend-unaccepted` and `health-admission-unaccepted` blockers. Named
required-evidence records are not accepted merely because a tool exists, a
property says `uke`, a file checksum matches or an imported JSON field says
`passed: true`. No advanced-mode switch bypasses admission. BitLocker and
SSH/network rescue remain deferred by the owner.

## Read-only slot and snapshot observation

On Android, the observer uses only these pinned bootctl queries:

- `get-number-slots`, `get-current-slot`, `get-active-boot-slot`;
- `get-snapshot-merge-status`;
- `is-slot-bootable` and `is-slot-marked-successful` for slots 0 and 1.

It makes at most twelve calls, each with a two-second deadline and the existing
bounded process output. It repeats the four global queries and the boot suffix
to detect an intervening transition. Equal reads do not constitute an atomic
snapshot. The running slot must agree with `_a`/`_b`; a different pending next
slot is held. Only an available exact `none` merge value is considered idle.
Unknown, snapshotted, merging, cancelled, malformed, timed-out or unavailable
values retain blockers. A fallback flag requires both a successful bootable
query and a successful marked-good query returning `1`. These HAL flags do not
verify stock bytes, physical durability or an actual fallback boot.

The observer never invokes `set-active-boot-slot`, marks a slot successful,
changes merge status or starts a snapshot merge. The
[AOSP Virtual A/B contract](https://source.android.com/docs/core/ota/virtual_ab/implement)
requires boot-control and snapshot coordination; an idle HAL value alone is
insufficient. The
[dynamic partition contract](https://source.android.com/docs/core/ota/dynamic_partitions/implement)
also separates target-slot metadata and shared super storage. An inactive A/B
slot is not an isolated second Android installation.

## Bounded health observations

The report reads only standard battery, thermal and UFS health attributes.
Contained sysfs aliases are resolved with `RESOLVE_IN_ROOT` and
`RESOLVE_NO_MAGICLINKS`. FIFO/device nodes, escaping aliases, NUL bytes and values
over 256 bytes are refused. Power-supply, thermal and platform enumeration are
bounded to 64, 128 and 512 entries respectively. Missing, denied and malformed
values remain visible; enumeration errors are retained.

Battery capacity is parsed as 0–100 percent, using the
[Linux power-supply ABI](https://github.com/torvalds/linux/blob/v6.12/Documentation/ABI/testing/sysfs-class-power).
The battery temperature ABI uses tenths of a Celsius degree. Generic thermal
attributes retain their advertised units but have `calibrated_channel: false`;
the stock audit's `vbat` channel must not be interpreted as a measured temperature.
The reader discovers UFS controller names and reads only `eol_info`,
`life_time_estimation_a` and `life_time_estimation_b` under `health_descriptor`.
It never queries vendor calibration/debug/reset endpoints or writes attributes.

No reviewed Uke channel/threshold/duration policy is configured. Accordingly the
health decision stays `hold`, even with apparently good raw readings. No battery
cutoff, thermal threshold or remaining-life guarantee is invented. Fresh
measurements and an operation-specific completion/hold policy are required
before physical writing can be admitted. FBE access additionally requires the
[installed metadata encryption and KeyMint chain](https://source.android.com/docs/security/features/encryption/metadata)
before credential use, mapper creation or an encrypted mount.

## Offline declaration controls

The host-only `platform compare-fixture CONTRACT --observation OBSERVATION`
command validates exact bounded schemas. Its context includes the profile,
commercial model/model number, product/device, hardware/vendor SKU, firmware
version/fingerprint and unit/boot/kernel hashes. Every required artifact digest
must match its expected requirement and the exact canonical context hash.
Unknown/duplicate fields, requirements or evidence are refused. Missing, stale,
failed and foreign-context evidence cannot pass a complete comparison.

The digest is an integrity binding, not authentication. The comparator neither
opens nor verifies the declared artifacts. Its report explicitly sets
`comparison_is_artifact_verification: false` and
`comparison_is_authentication: false`. Self-asserted records cannot enter a
trusted registry: no such registry is configured. In Android the fixture command
is refused before reading a contract or observation.

Android mutation commands `fbe-open`, `slot-set`, `snapshot-merge`, `super-apply`,
`ota-install` and `second-install` are explicit unavailable routes. They refuse
with `platform-action-unavailable` before reading requests/journals, opening
targets/roots, using credentials, mounting or calling a HAL writer. These names
document unavailable actions, not working device implementations.

## Validation and release boundary

`tests/check-platform.sh native|sanitizer` records frozen before/after sources,
compiler/binary hashes, five named CTest results and actual CLI controls. Tests
cover regional commercial/firmware identities, unit/boot/kernel substitutions,
all non-idle snapshot states, stale query text, fallback flags, every required
artifact/context digest, unsafe telemetry and actual OrangeFox callbacks.
A separate Android-compiled host ABI test wraps only read-only tool calls and
checks the twelve-call/deadline policy, changed reads and timeouts. This wrapper
is absent from Android.bp and provides no runtime configuration bypass.

The full-function guest script includes matching platform/early-refusal checks,
but adding those checks is not a guest pass. The release policy requires the
`platform-admission` capability and matching current core-guest evidence for
the `function-reviewed` class. Earlier guest receipts and historical recovery
images do not validate this source revision. Fresh target/image/package/VM and
separately authorized physical tests with a preserved fallback remain required.
