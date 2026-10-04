#!/usr/bin/env bash
# Runs after the service-owned heavy-job lock is acquired, before heavy work.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
label=${1:?Job mode required}
unit=${2:?Owned systemd unit required}
shift 2
[[ $unit =~ ^uke-recovery-[a-z0-9-]+$ && $# -gt 0 ]]
membership=$(awk -F: '$1==0 {print $3}' /proc/self/cgroup)
[[ $(systemctl --user show "$unit" --property=ControlGroup --value) == "$membership" ]]
cg=/sys/fs/cgroup$membership
[[ $(cat "$cg/memory.oom.group") == 1 ]]
policy=/tmp/admitted-policy.json
bash "$component/scripts/host-budget-policy.sh" "$label" /proc /sys/fs/cgroup owned > "$policy"
maximum=$(jq -r .memory_max_mib "$policy")
high=$(jq -r .memory_high_mib "$policy")
systemctl --user set-property --runtime "$unit" "MemoryMax=$((maximum*1048576))" "MemoryHigh=$((high*1048576))"
[[ $(cat "$cg/memory.max") == $((maximum*1048576)) && $(cat "$cg/memory.high") == $((high*1048576)) ]]
bash "$component/scripts/host-temp-policy.sh" "$label" /tmp /tmp/host-temp-policy.json > /tmp/host-temp-admitted.json
export UKE_HOST_JOBS GOMAXPROCS GOMEMLIMIT GOGC=40 UKE_HOST_BUDGET_ACTIVE=1 TMPDIR=/tmp TMP=/tmp TEMP=/tmp
UKE_HOST_JOBS=$(jq -r .compile_jobs "$policy")
GOMAXPROCS=$(jq -r .soong_procs "$policy")
GOMEMLIMIT="$(jq -r .go_heap_mib "$policy")MiB"
snapshot() {
    local phase=$1
    for field in memory.current memory.peak memory.events memory.pressure memory.stat pids.peak cpu.stat; do
        cat "$cg/$field" > "/tmp/$field.$phase"
    done
    cat /proc/pressure/memory > "/tmp/host-memory.pressure.$phase"
    ccache --print-stats > "/tmp/native-ccache.$phase"
    local android_cache="$component/src/upstream/orangefox-android16/out-public/ccache"
    if [[ -d $android_cache ]]; then CCACHE_DIR="$android_cache" ccache --print-stats > "/tmp/android-ccache.$phase"; fi
}
snapshot before
status=0
if [[ -x /usr/bin/time ]]; then
    /usr/bin/time -v -o /tmp/command-rusage "$@" || status=$?
else
    printf 'Host time utility unavailable; no command RSS receipt.\n' > /tmp/command-rusage
    "$@" || status=$?
fi
snapshot after
awk 'NR==FNR {b[$1]=$2; next} ($1=="oom" || $1=="oom_kill" || $1=="oom_group_kill") && $2!=b[$1] {exit 1}' \
    /tmp/memory.events.before /tmp/memory.events.after || { echo 'Owned host job encountered a cgroup OOM event.' >&2; status=1; }
printf '%s\n' "$status" > /tmp/command-status
exit "$status"
