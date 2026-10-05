#!/usr/bin/env bash
# Host policy fixtures and small actual cgroup/cache jobs; no tablet/block writes.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(mktemp -d "$component/build/host-budget-fixture-XXXXXX")
policy="$component/scripts/host-budget-policy.sh"
mkdir -p "$scratch/proc/self" "$scratch/cgroup/scope"
snapshot() {
    printf 'MemTotal: %s kB\nMemAvailable: %s kB\n' "$(( $1*1024 ))" "$(( $2*1024 ))" > "$scratch/proc/meminfo"
    printf 'Cpus_allowed_list: 0-15\n' > "$scratch/proc/self/status"
    printf '0::/scope\n' > "$scratch/proc/self/cgroup"
    printf '%s\n' "$3" > "$scratch/cgroup/scope/memory.max"
    printf '%s\n' "$4" > "$scratch/cgroup/scope/memory.high"
    printf '%s\n' "$5" > "$scratch/cgroup/scope/memory.current"
}
plan() { bash "$policy" "$1" "$scratch/proc" "$scratch/cgroup" "${2:-caller}"; }
refuse() { if "$@" > "$scratch/refused.log" 2>&1; then echo 'Expected host admission refusal' >&2; exit 1; fi; }
snapshot 16384 16384 max max 0
plan arm64 > "$scratch/16g.json"
jq -e '.shared_reserve_mib==4096 and .memory_max_mib==12288 and .memory_high_mib==11673 and .memory_high_fraction==0.95 and .compile_jobs==13 and .soong_procs==6 and .go_heap_mib==6144' "$scratch/16g.json" >/dev/null
plan sanitizer | jq -e '.compile_jobs==8 and .memory_max_mib==12288 and .memory_high_mib==10444 and .memory_high_fraction==0.85' >/dev/null
plan native | jq -e '.memory_max_mib==12288 and .memory_high_mib==10444 and .memory_high_fraction==0.85' >/dev/null
snapshot 32768 28672 max max 0
plan arm64 | jq -e '.compile_jobs==16 and .shared_reserve_mib==8192 and .memory_max_mib==16384' >/dev/null
snapshot 16384 16384 "$((8*1024*1024*1024))" max "$((2*1024*1024*1024))"
plan native | jq -e '.effective_capacity_mib==8192 and .memory_max_mib==2048 and .compile_jobs==1' >/dev/null
refuse plan arm64
snapshot 16384 16384 "$((12*1024*1024*1024))" "$((6*1024*1024*1024))" "$((2*1024*1024*1024))"
refuse plan native
snapshot 32768 28672 "$((8*1024*1024*1024))" "$((7*1024*1024*1024))" 0
plan native owned | jq -e '.memory_max_mib==8192 and .shared_reserve_mib==8192' >/dev/null
mkdir -p "$scratch/cgroup/scope/job"
printf '0::/scope/job\n' > "$scratch/proc/self/cgroup"
printf 'max\n' > "$scratch/cgroup/scope/job/memory.high"
printf '10737418240\n' > "$scratch/cgroup/scope/job/memory.max"
printf '0\n' > "$scratch/cgroup/scope/job/memory.current"
plan native owned | jq -e '.memory_max_mib==3072 and .effective_capacity_mib==7168' >/dev/null
snapshot 32768 28672 max max 0
printf 'Cpus_allowed_list: 2,4-7,10\n' > "$scratch/proc/self/status"
plan native | jq -e '.affinity=="2,4,5,6,7,10" and .allowed_cpu_count==6 and .compile_jobs==6' >/dev/null
printf 'Cpus_allowed_list: 7-4\n' > "$scratch/proc/self/status"
refuse plan native
snapshot 16384 4096 max max 0
refuse plan native
printf 'MemTotal: 1 kB\nMemAvailable: 2 kB\n' > "$scratch/proc/meminfo"
refuse plan native
snapshot 16384 16384 invalid max 0
refuse plan native
snapshot 16384 16384 max max 0
printf '0::/scope/../escape\n' > "$scratch/proc/self/cgroup"
refuse plan native
snapshot 16384 16384 max max 0
mv "$scratch/cgroup/scope/memory.current" "$scratch/current.saved"
refuse plan native
[[ $(bash "$policy" --compile-jobs 16 3) == 3 ]]
[[ $(bash "$policy" --compile-jobs 2 8) == 2 ]]
refuse bash "$policy" --compile-jobs 17 3
refuse bash "$policy" --compile-jobs '1+1' 3
printf 'Host snapshot controls passed: 16 GiB reserve, j16 ceiling, sanitizer cost, ancestor high/max, owned-leaf cap, pressure/CPU/input refusal and compile clamping.\n'

cat > "$scratch/cache-job.sh" <<'CACHE_JOB_EOF'
set -euo pipefail
work=$1
mkdir -p "$work/cache"
export CCACHE_DIR="$work/cache" CCACHE_COMPILERCHECK=content CCACHE_SLOPPINESS= CCACHE_MAXSIZE=64M
unset CCACHE_DISABLE CCACHE_BASEDIR CCACHE_IGNOREOPTIONS CCACHE_NODIRECT
printf '#include <vector>\nint result(){return std::vector<int>{1,2,3}.size();}\n' > "$work/fixture.cpp"
ccache --zero-stats >/dev/null
ccache /usr/bin/c++ -O2 -c "$work/fixture.cpp" -o "$work/fixture.o"
ccache --print-stats > "$work/cold-cache.stats"
before=$(sha256sum "$work/fixture.o" | cut -d' ' -f1)
rm "$work/fixture.o"
ccache /usr/bin/c++ -O2 -c "$work/fixture.cpp" -o "$work/fixture.o"
ccache --print-stats > "$work/warm-cache.stats"
[[ $(sha256sum "$work/fixture.o" | cut -d' ' -f1) == "$before" ]]
awk '$1=="direct_cache_hit" || $1=="preprocessed_cache_hit" {hits+=$2} END {exit hits<1}' "$work/warm-cache.stats"
printf 'Real compile admission: compile=%s, soong=%s, heap=%s; cold/warm object identity and cache hit passed.\n' "$UKE_HOST_JOBS" "$GOMAXPROCS" "$GOMEMLIMIT"
CACHE_JOB_EOF
bash "$component/scripts/with-host-budget.sh" cache-probe bash "$scratch/cache-job.sh" "$scratch" > "$scratch/service.log" 2>&1
cat "$scratch/service.log"
refuse bash "$component/scripts/with-host-budget.sh" failure-probe bash -c 'exit 7'
printf 'Actual service limits, post-lock admission, PSI/OOM/cache receipts and nonzero-command refusal passed; small reference-host fixtures only.\n'
