#!/usr/bin/env bash
# Host-only resource isolation. Serialize heavy jobs and keep temporary image
# fixtures on disk, outside the desktop app's memory cgroup and RAM-backed /tmp.
set -euo pipefail
export LC_ALL=C LANG=C
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
label=${1:?Pass a short job label and a command}
shift
[[ $label =~ ^[a-z][a-z0-9-]{0,40}$ && $# -gt 0 ]]
[[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]] || {
    echo 'Nested heavy-job admission refused; the existing service already owns the job lock.' >&2; exit 1;
}
for command in systemd-run systemctl bwrap flock taskset ccache jq rg stat sync; do command -v "$command" >/dev/null; done
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
service_status=0
systemd-run --user --quiet --wait --pipe --unit="$unit" \
    -p "WorkingDirectory=$component" -p "MemoryHigh=$((high*1048576))" -p "MemoryMax=$((maximum*1048576))" \
    -p MemorySwapMax=512M -p CPUQuota=1600% -p TasksMax=2048 -p OOMPolicy=kill -p Nice=5 \
    /usr/bin/flock "$component/build/host-budget/heavy.lock" \
    /usr/bin/env "PATH=$PATH" LC_ALL=C LANG=C \
    "CCACHE_DIR=$component/build/ccache/native" CCACHE_MAXSIZE=10G \
    taskset -c "$affinity" bwrap --bind / / --dev-bind /dev /dev --bind "$scratch" /tmp --proc /proc --chdir "$component" \
    bash "$component/scripts/run-host-budget-job.sh" "$label" "$unit" "$@" || service_status=$?
# An OOM group kill also terminates the in-service recorder. Preserve the
# manager's outcome outside that group before retiring this exact failed unit.
# Missing command/after records are never interpreted as successful completion.
systemctl --user show "$unit" --property=LoadState,Result,ExecMainCode,ExecMainStatus,MemoryPeak,MemorySwapPeak \
    > "$scratch/service-outcome.properties" 2> "$scratch/service-outcome.stderr" || true
manager_observed=false
[[ $(awk -F= '$1=="LoadState" {print $2}' "$scratch/service-outcome.properties") != loaded ]] || manager_observed=true
jq -cn --arg unit "$unit" --argjson status "$service_status" \
    --argjson observed "$manager_observed" \
    --rawfile properties "$scratch/service-outcome.properties" \
    '{schema_version:1,unit:$unit,launcher_status:$status,manager_observed:$observed,manager_properties:$properties}' \
    > "$scratch/service-outcome.json"
if (( service_status != 0 )); then
    # Reset only the service created above, after retaining its failed outcome.
    systemctl --user reset-failed "$unit" >/dev/null 2>&1 || true
    if [[ $(awk -F= '$1=="Result" {print $2}' "$scratch/service-outcome.properties") == oom-kill ]]; then
        echo 'Host job stopped by its cgroup OOM limit; the external service receipt is retained.' >&2
    else
        echo 'Host job failed; the external service outcome and any command receipts are retained.' >&2
    fi
    exit "$service_status"
fi
[[ -f $scratch/command-status && $(cat "$scratch/command-status") == 0 ]] || {
    echo 'Host job was interrupted before recording command completion.' >&2; exit 1;
}
if [[ -f $scratch/android-build-pending.json ]]; then
    bash "$component/scripts/build-evidence.sh" acknowledge "$scratch"
fi
# The scratch directory intentionally survives interruption for diagnostics.
# It contains only fixtures made by this job; review before removing it.
echo 'Resource-isolated host job completed; temporary fixtures remain in the private build directory.'
