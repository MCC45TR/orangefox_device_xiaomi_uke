# Recovery installation transactions

The legacy `uke-recovery-install install` entry is unavailable before opening
any input or device. Its volatile `/tmp` backup and raw block-copy path were
removed. Changing the common live-write policy cannot revive that writer.
`check` remains a read-only inspection of the pinned Global stock303 catalog;
it does not establish the installed model/SKU, physical geometry or a rehearsed
fallback route.

The native library and JSON CLI provide a separately labelled executable model
for **private regular images**. The request selects opposite `_a`/`_b` fixture
slots, an explicit profile and the exact proposed image SHA-256. Active,
inactive and staged recovery images have independent inodes and exactly
100 MiB capacity. A declared fixture profile is not firmware authority.

```text
installer image-prepare REQUEST --image ACTIVE --fallback-image INACTIVE
  --root STAGED --content-file recovery.img --backup NEW_STORE --output PLAN
installer image-execute PLAN --image ACTIVE --fallback-image INACTIVE
  --journal NEW_DIRECTORY --confirm INSTALLER_PLAN_SHA256
installer image-inspect JOURNAL --image ACTIVE --fallback-image INACTIVE
installer image-resume|image-rollback|image-cancel JOURNAL
  --image ACTIVE --fallback-image INACTIVE --confirm INSTALLER_PLAN_SHA256
```

Each prepared replacement explicitly records `reviewed-recovery-image` content
origin. The underlying raw restore backend binds the complete old/new hashes,
target inode/geometry/ownership, backup directory identity, profile, chunk
hashes and exact plan. The wrapper adds the immutable fallback content and
identity, selected fixture slots and its own confirmation digest. Inspecting
a raw journal must match that exact nested restore digest too.

Backup and journal admission requires writable ext4, Btrfs, F2FS or XFS with
successful directory synchronization. RAM, overlay and unknown filesystems are
refused. This establishes requested filesystem semantics; it does not prove
controller power-loss persistence, a host's storage persistence, or tablet
durability. No host acknowledgement or ordinary checksum is promoted to such
proof.

Before target writes, independently verified old and proposed chunks are
mirrored locally. Each written chunk is synchronized and read back, then the
complete target is synchronized again and independently verified before
`COMMITTED` or `ROLLED_BACK`. A no-rewrite resume also performs this last
synchronization: matching page-cache bytes cannot excuse a previous failed
flush. Exceptions retain an uncertain journal when writes may have occurred.
Recovery derives byte classification from original/desired data, not counters.
Complete mirrors permit source-independent resume and rollback.

Initial records use private temporary files, file fsync, no-replace atomic
publication and directory fsync. Forced termination during wrapper creation
permits retry only with the same reviewed plan, unchanged original bytes and
private unpublished artifacts. Interrupted raw initialization can be inspected
or cancelled after full original-byte verification. Resume retains that
incomplete directory under a new private archival name before rebuilding it.
Mirror initialization recognizes bounded, private unpublished record artifacts.
Foreign files and published conflicting plans are refused. Nothing deletes a
valid journal to pretend completion.

Raw creation uses the retained wrapper directory descriptor. Path/descriptor
identity is checked before delegation, during raw checkpoints and before
wrapper outcome publication. Existing target and journal locks remain in
place; cross-operation ownership is the separate ordered AUD-005 remediation.

Every result keeps `physical_device:false`, `live_block_write:false`,
`firmware_identity_validated:false`, `fallback_route_rehearsed:false` and
`device_fallback_route_accepted:false`. Actual installation still needs exact
installed-profile/six-LUN admission, a persistent owner/durability contract and
a separately rehearsed fallback route for each commercial model. Image tests
cannot supply those facts.
