#!/usr/bin/env bash
# Host-only resource isolation. Serialize heavy jobs and keep temporary image
# fixtures on disk, outside the desktop app's memory cgroup and RAM-backed /tmp.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
label=${1:?Pass a short job label and a command}
shift
[[ $label =~ ^[a-z][a-z0-9-]{0,40}$ && $# -gt 0 ]]
for command in systemd-run bwrap flock taskset ccache; do command -v "$command" >/dev/null; done
[[ $(stat -fc %T /sys/fs/cgroup) == cgroup2fs ]]
mkdir -p "$component/build/host-budget" "$component/reports/private"
scratch=$(mktemp -d "$component/build/host-budget/$label-XXXXXX")
unit="uke-recovery-$label-${scratch##*-}"
unit=${unit,,}
affinity=$(awk '$1=="Cpus_allowed_list:" {n=split($2,g,","); count=0; for(i=1;i<=n && count<16;i++) {m=split(g[i],r,"-"); last=m==2?r[2]:r[1]; for(cpu=r[1];cpu<=last && count<16;cpu++) {printf "%s%d",count?",":"",cpu;count++}}}' /proc/self/status)
[[ $affinity =~ ^[0-9]+(,[0-9]+)*$ ]]
mkdir -p "$component/build/ccache/native"
cd "$component"
systemd-run --user --quiet --wait --pipe --collect --unit="$unit" \
    -p "WorkingDirectory=$component" -p MemoryHigh=14G -p MemoryMax=16G \
    -p MemorySwapMax=512M -p CPUQuota=1600% -p TasksMax=2048 \
    /usr/bin/flock "$component/build/host-budget/heavy.lock" \
    /usr/bin/env "PATH=$PATH" LC_ALL=C LANG=C GOMEMLIMIT=12GiB GOGC=40 GOMAXPROCS=16 UKE_HOST_JOBS=16 UKE_HOST_BUDGET_ACTIVE=1 \
    "CCACHE_DIR=$component/build/ccache/native" CCACHE_MAXSIZE=10G \
    taskset -c "$affinity" bwrap --bind / / --dev-bind /dev /dev --bind "$scratch" /tmp --proc /proc --chdir "$component" \
    bash -c 'set -euo pipefail; status=0; "$@" || status=$?; cg=/sys/fs/cgroup$(awk -F: '\''$1==0 {print $3}'\'' /proc/self/cgroup); for field in memory.peak memory.events pids.peak; do cat "$cg/$field" > "/tmp/$field"; done; printf "%s\n" "$status" > /tmp/command-status; exit "$status"' bash "$@"
[[ -f $scratch/command-status && $(cat "$scratch/command-status") == 0 ]] || {
    echo 'Host job was interrupted before recording command completion.' >&2; exit 1;
}
# The scratch directory intentionally survives interruption for diagnostics.
# It contains only fixtures made by this job; review before removing it.
echo 'Resource-isolated host job completed; temporary fixtures remain in the private build directory.'
