# Host build resources and compiler cache

Run one heavy job at a time through the host-only wrapper:

```sh
bash scripts/with-host-budget.sh arm64 bash scripts/build-public.sh 16
bash scripts/with-host-budget.sh native bash tests/run-native.sh
bash scripts/with-host-budget.sh sanitizer bash tests/check-sanitizers.sh
```

The public Android and native/sanitizer entry points enter this wrapper automatically. The systemd service owns the serialization lock until its work ends, including interruption of the calling desktop. Explicit nested admission refuses before waiting on that same lock. `j16` is a maximum; lower requested compile counts remain respected.

## Admission and memory reserve

`host-budget-policy.sh` reads host `MemTotal`/`MemAvailable`, allowed CPUs, and every readable ancestor's `memory.max`, `memory.high` and `memory.current`. It retains the larger of **4 GiB or 25% of effective capacity** for the desktop, kernel and other work. The job's hard ceiling is the minimum of remaining shared headroom and 16 GiB. Admission fails if accounting is unavailable or the remaining envelope is below 4 GiB for Android, or 1.5 GiB for native/sanitizer work.

The service recalculates admission after acquiring the heavy-job lock. Its already admitted leaf limit remains a ceiling; shared ancestors still contribute their limits, usage and reserve. The worker verifies that it belongs to the named unit, tightens `MemoryMax`/`MemoryHigh`, reads them back, and only then starts heavy work. No unit other than that newly created job is modified. `MemoryHigh` is 95% of the admitted maximum for Android graph jobs and 85% for other modes; it is a reclaim/throttling threshold, not a hard allocation limit. Swap is capped at 512 MiB and TasksMax at 2048. Nice 5 gives interactive work scheduling preference; OOMPolicy=kill and checked `memory.oom.group=1` group the owned job's failure.

These example snapshots demonstrate the policy, not measured recovery build peaks:

| Host / available RAM | Job ceiling | Normal compile ceiling | Sanitizer ceiling | Soong runtime |
|---|---:|---:|---:|---:|
| 16 / 16 GiB, no tighter ancestor | 12 GiB | 13 | 8 | 6 processors / 6 GiB soft heap |
| 16 / 12 GiB, no tighter ancestor | 8 GiB | 8 | 4 | 4 processors / 4 GiB soft heap |
| 32 / 28 GiB, no tighter ancestor | 16 GiB | 16 | 11 | 6 processors / 6 GiB soft heap |
| 8 GiB ancestor, 2 GiB charged | 2 GiB | 1 | 1 | Android admission refused |

Compile scheduling allows 768 MiB per normal worker or 1280 MiB per sanitizer worker after an internal reserve of up to 2 GiB. These are conservative scheduling estimates, not a guarantee that each translation unit fits. CPUs are limited to the caller's permitted set and at most 16. A full build can still fail its own limit; unrelated allocations can change shared pressure after admission. Neither snapshot fixtures nor small jobs diagnose the owner's earlier task shutdowns or establish responsive-desktop acceptance under a complete clean build.

## Separate Soong and compiler parallelism

The pinned host Go 1.23.4 runtime receives `GOGC=40`, a soft `GOMEMLIMIT` of half the job envelope capped at 6 GiB, and a separate `GOMAXPROCS` capped at eight and reduced with available memory/compile admission. `build-public.sh` clamps its requested `m -j` to the admitted compiler ceiling.

The reviewed Soong stack preserves all three variables through its clean builder environment. The Blueprint stack keeps the Go runtime's admitted concurrency instead of resetting it to `runtime.NumCPU()`, uses it for bootstrap/compiler scheduling, and carries runtime limits into microfactory compiler children. Active module visitors and provider-verification workers also follow that policy, retaining the upstream ceiling. Paused dependency visitors yield an active slot; every dependency, module and provider check remains enabled. CPU scheduling alone did not previously limit the 1,000 in-flight jobs in these pools. Exact upstream pins and reviewed prefixes are checked; unknown edits refuse without replacement. These are changes to existing upstream host tools. No Go runtime or new Go application is installed on the tablet.

Go's memory setting is a soft runtime target, separate from the cgroup's aggregate hard limit. Compilers, child processes and charged file cache also consume the job envelope. The [Go GC guide](https://go.dev/doc/gc-guide#Memory_limit) and [kernel cgroup interface](https://docs.kernel.org/admin-guide/cgroup-v2.html#memory-interface-files) define those separate contracts.

## Receipts and temporary storage

User systemd, cgroup v2, bubblewrap, taskset, flock, jq and ccache are required. Isolation/accounting failure refuses admission. The private job directory retains initial/admitted policy JSON, before/after cgroup peak/current/events/statistics, cgroup and host memory PSI, process/task counters, cache counters, and command status. If `/usr/bin/time` is available, command RSS/CPU accounting is retained separately. A nonzero command, new OOM event or missing completion record cannot become a passing receipt. A forced service kill can terminate its in-group recorder too. The outer launcher therefore saves its status and the manager's result/exit/peak properties before retiring only that exact failed unit. Missing final counters remain incomplete; manager metadata never substitutes for the zero-OOM build-completion gate.

Both namespaces bind the same private disk directory to `/tmp`; TMPDIR, TMP and TEMP point there. `host-temp-policy.sh` checks the resolved path's kernel filesystem magic, filesystem ID, directory device/inode, owner and mode before service admission, after the lock and inside the Android namespace. A different directory, unsupported filesystem, missing or altered receipt, readonly mount or failed create/write/filesystem-sync/unlink refuses before the build command. Identity comes from the resolved path, not the first mount-list entry: shadowed mounts may share the same target name. The [bubblewrap bind contract](https://github.com/containers/bubblewrap/blob/main/bwrap.xml) preserves a host path through the namespace.

Scratch requires a reviewed local ext2/3/4, XFS, Btrfs or F2FS filesystem, at least 8 GiB of available filesystem space for Android or 2 GiB for native/sanitizer jobs, and 131,072 available inodes where a fixed count exists. Btrfs's zero total/free inode reports are recorded as dynamic accounting, not exhaustion; the small real create/write/sync probe still must pass. Unknown accounting/filesystems, including tmpfs/ramfs, overlay, network and unreviewed FUSE mounts, refuse. These conservative minima are admission snapshots, not quotas or guarantees of future metadata/data allocation. Output-tree capacity, full build peaks and immutable output closure need separate validation.

The actual production inner mount recipe wrote a 64 MiB incompressible regular file on Btrfs. It allocated 67,108,864 bytes, increased charged file cache by 67,117,056 bytes, and showed a 67,117,056-byte aggregate filesystem availability drop after synchronization. Anonymous memory changed by -4,096 bytes and shmem by zero. The service peaked at 73,277,440 charged bytes, command RSS was 4,664 KiB and wall time 0.52 seconds; zero OOM events were recorded. File cache still consumes memory even with disk scratch. Filesystem-wide free-space changes can include unrelated work; file allocation and exact cross-namespace identity are the independent placement checks. This is a bounded host fixture, not a clean recovery build or guest/device acceptance.

## Compiler cache and validation

Android and native C++ caches are separate below the ignored build tree and capped at 10 GB each. `host-ccache.sh` checks compiler content and clears permissive time/path sloppiness. Ccache does not cache Go graph generation, linking, packaging, tests or VM execution. Record counters rather than assume a warm build has eliminated memory pressure. Each Android job owns a separate output. It starts empty unless an explicit, compatible unsealed project job supplies a provenance-bound reflink cache; installed payloads and images are invalidated before rebuilding. Reflinks avoid duplicating unchanged file extents, but changed objects, full content indexing and charged file cache still need disk and memory headroom. The existing accepted output stays available until the [build-completion gate](BUILD-COMPLETION.md) publishes the new one.

```sh
CCACHE_MAXSIZE=10G ccache --dir build/ccache/android --show-stats
CCACHE_MAXSIZE=10G ccache --dir build/ccache/native --show-stats
bash tests/check-host-budget.sh
bash tests/check-host-builder-patches.sh
bash tests/check-host-temp.sh
bash tests/check-build-evidence.sh
```

Run these standalone host checks outside an already active heavy-job service. They cover 16 GiB and constrained-ancestor policy snapshots, CPU/input/pressure refusal, actual service readback, failure propagation, a real cold/warm C++ object/cache oracle, pinned Blueprint package tests, runtime-policy retention and actual nested disk scratch. Worker controls use actual visitors under one/two/four-worker policies and retain changed, unset and unhashable provider refusals; dependency/pause tests also run under the race detector. The failure-receipt control reduces only its disposable service to 32 MiB and verifies the external OOM result after its internal recorder is killed. Scratch controls substitute tmpfs, a different disk directory and readonly mounts; each refuses before the command. These are bounded host fixtures. Fresh clean/warm Android builds, full-load RSS/PSI/OOM and interactive response receipts remain separate acceptance gates after immutable output closure (AUD-024). Source, package, guest and tablet acceptance remain independent.
