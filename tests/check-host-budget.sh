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
plan arm64 | jq -e '.effective_capacity_mib==16384 and .compile_jobs==16 and .shared_reserve_mib==4096 and .memory_max_mib==16384' >/dev/null
snapshot 32768 13000 max max 0
plan arm64 | jq -e '.effective_capacity_mib==16384 and .shared_reserve_mib==4096 and .memory_max_mib==8904 and .minimum_mib==8192' >/dev/null
snapshot 32768 12000 max max 0
refuse plan arm64
plan arm64-incremental | jq -e '.mode=="arm64-incremental" and .minimum_mib==6144 and .memory_max_mib==7904 and .admitted' >/dev/null
snapshot 32768 10240 max max 0
plan arm64-incremental | jq -e '.memory_max_mib==6144 and .minimum_mib==6144 and .admitted' >/dev/null
snapshot 32768 10239 max max 0
refuse plan arm64-incremental
bash "$policy" --android-mode arm64 fresh
bash "$policy" --android-mode arm64-incremental job-ABCDEFGHIJKL
refuse bash "$policy" --android-mode arm64-incremental fresh
refuse bash "$policy" --android-mode native job-ABCDEFGHIJKL
refuse bash "$policy" --android-mode arm64-incremental ../unowned
snapshot 16384 16384 "$((8*1024*1024*1024))" max "$((2*1024*1024*1024))"
plan native | jq -e '.effective_capacity_mib==8192 and .memory_max_mib==2048 and .compile_jobs==1' >/dev/null
refuse plan arm64
snapshot 16384 16384 "$((12*1024*1024*1024))" "$((6*1024*1024*1024))" "$((2*1024*1024*1024))"
refuse plan native
snapshot 32768 28672 "$((8*1024*1024*1024))" "$((7*1024*1024*1024))" 0
plan native owned | jq -e '.memory_max_mib==8192 and .shared_reserve_mib==4096' >/dev/null
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
printf 'Host snapshot controls passed: 16 GiB work envelope, desktop reserve, Soong minimum, j16 ceiling, sanitizer cost, ancestor high/max, owned-leaf cap, pressure/CPU/input refusal and compile clamping.\n'

cat > "$scratch/cache-job.sh" <<'CACHE_JOB_EOF'
set -euo pipefail
work=$1
[[ $LC_ALL == C && $LANG == C ]]
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
cat > "$work/oom.cpp" <<'CPP_EOF'
#include <sys/mman.h>
#include <unistd.h>
int main() {
    alarm(15);
    constexpr unsigned long size = 96UL * 1024 * 1024;
    void* mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return 2;
    auto* bytes = static_cast<volatile unsigned char*>(mapping);
    for (unsigned long offset = 0; offset < size; offset += 4096) bytes[offset] = 1;
    munmap(mapping, size);
    return 0;
}
CPP_EOF
ccache /usr/bin/c++ -std=c++20 -O2 -Wall -Wextra -Werror "$work/oom.cpp" -o "$work/oom-probe"
CACHE_JOB_EOF
LC_ALL=tr_TR.UTF-8 LANG=tr_TR.UTF-8 bash "$component/scripts/with-host-budget.sh" native bash "$scratch/cache-job.sh" "$scratch" > "$scratch/service.log" 2>&1
cat "$scratch/service.log"
before_failures=$(find "$component/build/host-budget" -maxdepth 1 -type d -name 'failure-probe-*' | sort)
refuse bash "$component/scripts/with-host-budget.sh" failure-probe bash -c 'exit 7'
mapfile -t failure_record < <(comm -13 <(printf '%s\n' "$before_failures" | sed '/^$/d') \
    <(find "$component/build/host-budget" -maxdepth 1 -type d -name 'failure-probe-*' | sort))
[[ ${#failure_record[@]} == 1 ]]
jq -e '.schema_version==1 and .launcher_status==7 and .manager_observed and (.manager_properties|contains("Result=exit-code"))
    and (.manager_properties|contains("ExecMainStatus=7"))' "${failure_record[0]}/service-outcome.json" >/dev/null
[[ $(cat "${failure_record[0]}/command-status") == 7 ]]
cat > "$scratch/oom-job.sh" <<'OOM_JOB_EOF'
set -euo pipefail
membership=$(awk -F: '$1==0 {print $3}' /proc/self/cgroup)
unit=${membership##*/}
[[ $unit == uke-recovery-oom-probe-*.service ]]
# This negative control only reduces its own admitted envelope. It cannot
# change an unrelated job, a parent limit or the desktop's memory allowance.
systemctl --user set-property --runtime "$unit" MemoryMax=32M MemoryHigh=32M MemorySwapMax=0
cg=/sys/fs/cgroup$membership
[[ $(cat "$cg/memory.max") == 33554432 && $(cat "$cg/memory.high") == 33554432 && $(cat "$cg/memory.swap.max") == 0 ]]
printf 'Controlled OOM probe: own maximum/high=32 MiB, swap=0, executable alarm=15 seconds.\n' > /tmp/oom-probe-policy.txt
exec "$1"
OOM_JOB_EOF
before_oom=$(find "$component/build/host-budget" -maxdepth 1 -type d -name 'oom-probe-*' | sort)
refuse bash "$component/scripts/with-host-budget.sh" oom-probe bash "$scratch/oom-job.sh" "$scratch/oom-probe"
mapfile -t oom_record < <(comm -13 <(printf '%s\n' "$before_oom" | sed '/^$/d') \
    <(find "$component/build/host-budget" -maxdepth 1 -type d -name 'oom-probe-*' | sort))
[[ ${#oom_record[@]} == 1 ]]
jq -e '.schema_version==1 and .launcher_status!=0 and .manager_observed and (.manager_properties|contains("Result=oom-kill"))' \
    "${oom_record[0]}/service-outcome.json" >/dev/null
[[ -f ${oom_record[0]}/oom-probe-policy.txt && ! -f ${oom_record[0]}/command-status ]]
printf 'Actual service/cache admission, exact nonzero status and external group-OOM outcome passed; the 32 MiB killed fixture cannot supply build or tablet acceptance.\n'
