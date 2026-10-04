# Recovery operation ownership

The common coordinator serializes cooperating native management calls and stock
recovery lifecycle transitions. A GUI mutex or an individual journal lock does
not provide that boundary. An independently invoked CLI must use the same
domain as the GUI, installer, raw restore, GPT and filesystem managers.

## Current admission

The implementation is a **host fixture backend**. Android operation admission
returns `ownership-unavailable` before opening a coordinator or performing a
management effect. The Android environment and host configuration API cannot
enable it. Stock recovery lifecycle remains usable while the compile-time
management backend is unavailable. Accepting a device backend requires the same
persistent domain in both management and every lifecycle entry point; changing
that constant without wiring the latter fails compilation.

Host invocations select one absolute `URE_OPERATION_COORDINATOR` path before
first use. Configuration cannot change after admission. The private directory
and its single-link lock/record files must have mode 0700/0600, the current
owner, stable descriptor/path identities, and an accepted ext4, Btrfs, F2FS or
XFS filesystem. This filesystem allowlist is a fixture policy, not proof that
tablet writes survive a forced restart. Independent user namespaces use their
own test environment because ownership identities have different UID mappings.

## Operation lifetime

- Admission takes process-independent exclusion before individual target or
  journal writer locks. The immutable binding contains the operation/request,
  exact reviewed plan hash, every target identity and journal parent/path.
- Preparation and coherent reads hold exclusion without publishing target
  intent. A failed preparation cannot strand an unstarted target mutation.
- Immediately before an installed-target effect or child/mount lifetime,
  `checkpoint` publishes an atomic, synced `owner.json`. Destruction releases
  the kernel lock but preserves unresolved intent. Process death, elapsed time
  and a released `flock` do not authorize a new operation.
- Nested helpers receive an explicit parent token and revalidate their derived
  plans, descriptors and target coverage. A PID, thread-local flag or shared
  open file description does not grant implicit borrowing. Forked children
  cannot exercise the parent's token.
- Recovery adopts only the exact retained binding. Btrfs controls have a
  separate owner-bound control token, including the captured root/FSID and
  running plan/journal. They cannot retire the owner's lifetime.
- Completion requires the caller's independent bytes/metadata oracles and
  confirmed child/mount cleanup. It records the terminal receipt and a durable
  completion marker while the original owner still exists, then removes the
  owner. Failed completion publication retains the original owner even when
  every restoration write fails. After that commit point, an absent owner is
  accepted only with its exact verified receipt. The process that observed an
  error remains conservatively closed until exact recovery verifies it again.

`operation status` is read-only and never creates a domain. Its detailed owner
record is private diagnostic data. It reports observation limits rather than
claiming an atomic multi-file snapshot.

## Lifecycle and recovery paths

The reviewed patches cover complete stock mount/bind/unmount callbacks,
related-partition unmounts, the queued GUI reboot handoff, direct reboot,
`fs_mgr` unmount and fastbootd shutdown/reboot handlers. An explicit lifecycle
token survives each callback's effects and related nested calls. New mounts and
unmount/reboot requests are refused while an operation is active or unresolved.
Direct bind mounts also require a read-only source.

File/GPT, raw and streamed restore, compound partition/filesystem/six-LUN stock
jobs and boot routing pass the common binding through their internal writers.
A valid foreign raw plan cannot replace the filesystem application's persisted
before/after commitment. Streamed restore has a durable `VALIDATED` journal
before publishing its between-call ownership reservation.

Tree restore persists a separate restore plan/state before staging. Recovery
never identifies a published directory solely by matching content. It requires
the captured staging inode, exact backup/journal/destination identities, all
archived bytes and metadata, hardlink relationships and the complete directory
namespace. Archived runtime sockets are intentionally omitted. Inspect with:

```text
uke-recoveryctl backup tree-recover STORE RESTORE_PLAN_RECORD inspect --destination DEST
```

The same command accepts `verify-published` or `cancel-unpublished` with
`--confirm RESTORE_PLAN_SHA256`. Cancellation preserves unpublished staging as
an explicit artifact; it does not recursively delete unknown entries. A stage
created before its inode record was saved proves no installed destination only
when that destination remains absent and the exact native lifetime is exclusive.

Rescue closes its namespace worker and PID-namespace descendants before
releasing the parent lease. Writable sessions sync each written filesystem;
this is lifetime verification, not an atomic package repair or a contents
rollback oracle. A private pre-init failure message plus a reaped worker proves
that no init/payload/mount lifetime began. Failed `fork` follows a distinct
no-worker cleanup path. If the lifetime or required sync evidence is missing,
ownership remains unresolved; PID absence alone cannot clear it. Aggregate
resource admission and recovery after lost rescue supervision remain separate
acceptance work.

## Limits and validation

Cooperating software exclusion does not constrain an unrestricted root shell,
raw ADB command or another program that ignores the protocol. Storage identity,
mount/snapshot and loop-alias checks remain necessary and are revalidated at the
write boundary. Advisory locks are not a substitute for supported exclusive
block-device opens. This change does not accept physical block writes, device
firmware profiles, tablet kernel capabilities or installed-OS repair.

Focused host fixtures exercise independent CLI-style processes, exact recovery,
target/journal replacement, concurrent checkpoint/retirement, active and retained
lifecycle refusals, real SIGKILL before/after terminal commit, short/failing I/O,
and production callbacks with every device effect mocked. Generic guest,
shipping build and own-device acceptance must each retain their own evidence.
