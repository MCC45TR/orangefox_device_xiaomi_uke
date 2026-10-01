# Host-assisted raw image restore

This checkpoint implements the streaming restore path in roadmap §29. The
write backend accepts disposable regular storage images with 512/4096-byte
geometry. Physical block writes remain blocked by the common firmware/slot/
snapshot and ownership gate. No tablet, electrical power-loss, cross-boot or
GUI rendering acceptance is established.

The tablet-side process keeps private manifests and a bounded chunk cache,
rather than both complete raw objects. The host must hold complete verified
original and desired stores throughout restoration and rollback. Compression,
Android sparse input, multi-LUN orchestration and live writes remain unfinished.

## Review and trust

`restore stream-plan` checks the desired schema-2 storage manifest, declared
firmware profile, inode or unit/LUN identity, capacity, sector geometry and known
disk GUID. It scans the current object twice and seals both manifests in a
separate plan. A declared profile is not installed-firmware verification. The
combined plan is limited to 4 MiB; choose a supported larger chunk size when the
object would exceed the manifest budget. Chunks are 64 KiB–64 MiB and transfer
buffers remain 64 KiB.

Before starting a journal, `restore host-receipt` verifies every chunk and both
full hashes in private host stores. It binds their directory identities to the
reviewed plan. Its receipt is **HOST_ATTESTED**: a checksum is not a signature or
independent proof that remote storage is durable. The operator and authenticated
host transport are trusted. The device independently verifies every received
original/desired pair and the complete final target. It never claims that it has
independently inspected the host disk.

The native GUI creates a plan from a desired manifest copied to recovery and
shows the metadata paths, target identity, journal path and required cache
space. It can inspect journals, record a reviewed rollback direction, finish an
already complete readback, or cancel safely before writes. Remaining data
transfer requires the host companion; the GUI does not start a network service.
Copy the full saved plan to the host for review. Display summaries omit chunk
lists, while the saved sealed plan retains them.

## Host companion

First create the reviewed plan on the selected image's system. A desired
manifest must describe this exact original target, rather than another copied
image with a different inode.

```sh
uke-recoveryctl restore stream-plan /tmp/desired-manifest.json \
  --image /mnt/rescue/disk.img --sector-size 4096 \
  --profile global-os3.0.303.0 --output /tmp/stream-plan.json
```

Copy that private plan to the host. Use an existing authenticated connection
with a verified host key; the companion does not configure SSH or copy keys.
The host command below illustrates regular-image operation only:

```sh
bash scripts/restore-from-host.sh --start \
  --plan /mnt/host/stream-plan.json --source-plan /tmp/stream-plan.json \
  --source-before-plan /tmp/original-manifest.json \
  --source-journal /mnt/rescue/stream-journal \
  --before /mnt/host/original-store --after /mnt/host/desired-store \
  --image /mnt/rescue/disk.img --sector-size 4096 \
  --confirm REVIEWED_PLAN_SHA256 --ssh recovery@device
```

`--start` first captures the complete original store through the existing host
receiver, verifies both host stores, saves the host attestation and starts the
device journal. No target write precedes those checks. A retry can reuse the
same original metadata file only when its complete manifest agrees. A partial
host capture resumes from verified chunks; stale source bytes or identity stop
the retry. If journal creation succeeded, use `--resume` instead of `--start`.

After a disconnect, repeat the same arguments with `--resume`. For a reviewed
rollback, use `--rollback`; an interrupted rollback keeps its recorded direction
and can be continued with `--rollback`. Both stores and their original
directory identities must remain available. A store replacement requires a new
review instead of silent rebinding. The companion holds shared locks, rechecks
directory identities, verifies host data before reconnecting and exports each
stored chunk with another hash check.

`--local` runs the same protocol on the host's disposable image. ADB requires
`--adb-serial SERIAL` and an advertised `shell_v2` feature. It uses
`adb -s SERIAL shell -T -e none` to carry raw stdin, stdout and the remote exit
status without a TTY or escape processing. `exec-in` and `exec-out` cannot carry
both sides of this verified transaction. This choice is grounded in the pinned
Android `packages/modules/adb/client/commandline.cpp` implementation. SSH uses
`-T`, `BatchMode=yes` and `StrictHostKeyChecking=yes`. Every remote argument is
quoted independently; manifest text never becomes shell code. Transport tests
use mocks and do not establish actual device USB or SSH acceptance.

## Durable chunk protocol and recovery

The packet is exactly one original chunk followed by one desired chunk and EOF.
The receiver bounds input type, size, a 30-second idle timeout and a ten-minute
total packet deadline. It checks both hashes, fsyncs and reads back both private
cache files before recording write intent. Only then does it overwrite the
selected target range, fsync it, check readback and persist progress.

At most one active pair and one incoming pair coexist. The conservative space
estimate is **four chunk sizes plus 16 MiB for metadata and margin**; it does
not grow with total raw-object size. A completed operation retains metadata,
not full local raw copies. Choose a verified persistent journal parent when
recovery across reboot matters; `/tmp` is volatile. Stable cross-boot live
identity and electrical power-loss behavior are not accepted here.

Inspection derives progress from current bytes, never a journal counter. Whole
chunks must match their original or desired hashes. An active partial chunk
additionally requires its retained, hash-verified original/desired cache and
per-byte membership in those two states. Other bytes classify as DIVERGED and
block continuation. A partial active chunk is resolved before switching to any
other chunk, including during rollback, so its sole durable proof is retained.
Cache replacement publishes the new durable intent before deleting the old
pair. Failed or killed input leaves the target untouched; failures after write
intent or inherited partial data record FAILED_UNCERTAIN. Orphan receive caches
are removed only after checking private ownership, type and size.

```sh
uke-recoveryctl restore stream-status /mnt/rescue/stream-journal \
  --image /mnt/rescue/disk.img --sector-size 4096
```

Safe cancellation requires original bytes and a pre-execution phase. Completion
requires every desired chunk plus a full final SHA-256. Rollback similarly
requires every original chunk and full readback. Target and journal locks only
serialize cooperating processes; they do not establish an atomic snapshot or
exclude hostile writers. Preserve the host originals until the outcome has been
independently reviewed.

## I/O and memory policy

Planning, explicit inspection/reconnect and final verification retain complete
content scans. Between those boundaries, ordinary image steps can reuse compact
readback classes only when their plan-bound checksum and complete current image
identity agree, including inode, size, ownership, mode, mtime and ctime. The
incoming range is still rehashed before writing and read back afterwards. Any
identity/proof mismatch falls back to full current-content inspection; active
partial writes always require the verified pair and current-byte scan. Restoring
mtime does not restore ctime or authorize reuse. The proof stores one character
per chunk, rather than a copy of every manifest hash.

This avoids reading an entire large image before and after every ordinary chunk.
Cached step responses explicitly report
`unchanged-image-metadata-and-per-chunk-readback` and leave `current_sha256` null;
they do not invent a full current-image hash. Final COMMITTED/ROLLED_BACK still
requires full readback. The cache shares the existing cooperating-writer trust
boundary; it is not protection against hostile processes modifying data and
private records concurrently.
