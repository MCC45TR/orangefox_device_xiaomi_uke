# Host build resources and compiler cache

Run one heavy job at a time through the host-only wrapper:

```sh
bash scripts/with-host-budget.sh arm64 bash scripts/build-public.sh 16
bash scripts/with-host-budget.sh native bash tests/run-native.sh
bash scripts/with-host-budget.sh sanitizer bash tests/check-sanitizers.sh
```

The public Android build and native/sanitizer test entry points also enter this
wrapper automatically when invoked directly. The service itself holds the job
lock, so interruption of the calling desktop does not release a running job's
serialization lock.

The owner's current host has 16 logical CPUs and approximately 30 GiB of RAM.
The reviewed policy uses up to 16 jobs, CPU affinity within the caller's allowed
CPUs, CPUQuota 1600%, MemoryHigh 14 GiB, MemoryMax 16 GiB, MemorySwapMax 512 MiB
and TasksMax 2048. The independent cgroup limit includes child processes and
charged file cache; it is not just a Go heap setting. The host needs memory
headroom for the desktop and kernel outside that limit.

The pinned Go 1.23.4 builder receives GOMEMLIMIT 12GiB and GOGC 40. Patch 0013
preserves these two values across Soong's clean-environment launcher. CPU affinity
also bounds Blueprint's runtime.NumCPU override. This changes the upstream host
builder; no Go runtime or new Go application is installed on the tablet.

User systemd, cgroup v2, bubblewrap, taskset, flock and ccache must be available.
The wrapper fails if isolation is unavailable. Each job gets a private,
disk-backed temporary directory below the ignored build tree, avoiding large
image fixtures on RAM-backed /tmp. A service-owned lock serializes heavy jobs. A command-status
receipt must exist before completion is reported: manually stopping a service
does not become a passing result. Interrupted scratch directories remain private
for diagnosis and reviewed cleanup.

Android and native host C++ builds use separate disk caches capped at 10 GB
each. `host-ccache.sh` uses compiler-content checks and clears permissive
time/path sloppiness from the pinned Android defaults. It does not cache Go
dependency-graph generation, linking, packaging, tests or VM execution. The
cache executable is a host dependency and never part of the recovery payload.

Inspect the Android cache after a build:

```sh
CCACHE_MAXSIZE=10G ccache --dir src/upstream/orangefox-android16/out-public/ccache --show-stats
CCACHE_MAXSIZE=10G ccache --dir build/ccache/native --show-stats
```

Incremental compilation and cache hits can reduce later build time, but do not
establish a clean, reproducible binary build. Record cache counters, exact source
inputs, peak memory and command results separately from repeated packaging and
device acceptance. Source receipts reject changes made during native tests.
