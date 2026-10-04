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

The service recalculates admission after acquiring the heavy-job lock. Its already admitted leaf limit remains a ceiling; shared ancestors still contribute their limits, usage and reserve. The worker verifies that it belongs to the named unit, tightens `MemoryMax`/`MemoryHigh`, reads them back, and only then starts heavy work. No unit other than that newly created job is modified. `MemoryHigh` is 85% of the admitted maximum; it is a reclaim/throttling threshold, not a hard allocation limit. Swap is capped at 512 MiB and TasksMax at 2048. Nice 5 gives interactive work scheduling preference; OOMPolicy=kill and checked `memory.oom.group=1` group the owned job's failure.

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

The reviewed Soong stack preserves all three variables through its clean builder environment. The Blueprint stack keeps the Go runtime's admitted concurrency instead of resetting it to `runtime.NumCPU()`, uses it for bootstrap/compiler scheduling, and carries runtime limits into microfactory compiler children. Exact upstream pins and reviewed prefixes are checked; unknown edits refuse without replacement. These are changes to existing upstream host tools. No Go runtime or new Go application is installed on the tablet.

Go's memory setting is a soft runtime target, separate from the cgroup's aggregate hard limit. Compilers, child processes and charged file cache also consume the job envelope. The [Go GC guide](https://go.dev/doc/gc-guide#Memory_limit) and [kernel cgroup interface](https://docs.kernel.org/admin-guide/cgroup-v2.html#memory-interface-files) define those separate contracts.

## Receipts and temporary storage

User systemd, cgroup v2, bubblewrap, taskset, flock, jq and ccache are required. Isolation/accounting failure refuses admission. The private job directory retains initial/admitted policy JSON, before/after cgroup peak/current/events/statistics, cgroup and host memory PSI, process/task counters, cache counters, and command status. If `/usr/bin/time` is available, command RSS/CPU accounting is retained separately. A nonzero command, new OOM event or missing completion record cannot become a passing receipt. A forced service kill can leave incomplete receipts; the private scratch directory is retained for diagnosis.

The outer wrapper binds disk storage below the ignored build tree to `/tmp`. **AUD-023 remains open:** the Android build's inner bubblewrap still overlays that location with tmpfs. Do not treat the outer binding alone as Android disk-scratch acceptance. Nested temporary storage and inode/space admission must be fixed before the next complete clean Android build.

## Compiler cache and validation

Android and native C++ caches are separate and capped at 10 GB each. `host-ccache.sh` checks compiler content and clears permissive time/path sloppiness. Ccache does not cache Go graph generation, linking, packaging, tests or VM execution. Record counters rather than assume a warm build has eliminated memory pressure.

```sh
CCACHE_MAXSIZE=10G ccache --dir src/upstream/orangefox-android16/out-public/ccache --show-stats
CCACHE_MAXSIZE=10G ccache --dir build/ccache/native --show-stats
bash tests/check-host-budget.sh
bash tests/check-host-builder-patches.sh
```

Run these standalone host checks outside an already active heavy-job service. They cover 16 GiB and constrained-ancestor policy snapshots, CPU/input/pressure refusal, actual service readback, failure propagation, a real cold/warm C++ object/cache oracle, pinned Blueprint package tests and actual runtime-policy retention. They use small synthetic work only. Fresh clean/warm Android builds, full-load RSS/PSI/OOM and interactive response receipts remain separate acceptance gates after AUD-023/024 closure. Source, package, guest and tablet acceptance remain independent.
