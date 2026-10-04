#!/usr/bin/env bash
# Host-only resource isolation. Serialize heavy jobs and keep temporary image
# fixtures on disk, outside the desktop app's memory cgroup and RAM-backed /tmp.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
label=${1:?Pass a short job label and a command}
shift
[[ $label =~ ^[a-z][a-z0-9-]{0,40}$ && $# -gt 0 ]]
[[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]] || {
    echo 'Nested heavy-job admission refused; the existing service already owns the job lock.' >&2; exit 1;
}
for command in systemd-run systemctl bwrap flock taskset ccache jq stat sync; do command -v "$command" >/dev/null; done
[[ $(stat -fc %T /sys/fs/cgroup) == cgroup2fs ]]
mkdir -p "$component/build/host-budget" "$component/reports/private"
chmod 0700 "$component/build/host-budget"
scratch=$(mktemp -d "$component/build/host-budget/$label-XXXXXX")
unit="uke-recovery-$label-${scratch##*-}"
unit=${unit,,}
bash "$component/scripts/host-temp-policy.sh" "$label" "$scratch" > "$scratch/host-temp-policy.json"
bash "$component/scripts/host-budget-policy.sh" "$label" > "$scratch/initial-policy.json"
affinity=$(jq -r .affinity "$scratch/initial-policy.json")
[[ $affinity =~ ^[0-9]+(,[0-9]+)*$ ]]
maximum=$(jq -r .memory_max_mib "$scratch/initial-policy.json")
high=$(jq -r .memory_high_mib "$scratch/initial-policy.json")
mkdir -p "$component/build/ccache/native"
cd "$component"
systemd-run --user --quiet --wait --pipe --collect --unit="$unit" \
    -p "WorkingDirectory=$component" -p "MemoryHigh=$((high*1048576))" -p "MemoryMax=$((maximum*1048576))" \
    -p MemorySwapMax=512M -p CPUQuota=1600% -p TasksMax=2048 -p OOMPolicy=kill -p Nice=5 \
    /usr/bin/flock "$component/build/host-budget/heavy.lock" \
    /usr/bin/env "PATH=$PATH" LC_ALL=C LANG=C \
    "CCACHE_DIR=$component/build/ccache/native" CCACHE_MAXSIZE=10G \
    taskset -c "$affinity" bwrap --bind / / --dev-bind /dev /dev --bind "$scratch" /tmp --proc /proc --chdir "$component" \
    bash "$component/scripts/run-host-budget-job.sh" "$label" "$unit" "$@"
[[ -f $scratch/command-status && $(cat "$scratch/command-status") == 0 ]] || {
    echo 'Host job was interrupted before recording command completion.' >&2; exit 1;
}
# The scratch directory intentionally survives interruption for diagnostics.
# It contains only fixtures made by this job; review before removing it.
echo 'Resource-isolated host job completed; temporary fixtures remain in the private build directory.'
