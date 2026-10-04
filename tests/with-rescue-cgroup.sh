#!/usr/bin/env bash
# Host-only enforcement laboratory. Only a newly created delegated user scope
# is changed; production never accepts this wrapper or a controller override.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${1:-} == --namespace ]]; then
    shift
    selected=${1:?Fresh scope path is required}
    shift
    [[ $selected == /sys/fs/cgroup/user.slice/*/ure-rescue-test-*.scope && $# -gt 0 ]]
    [[ $(stat -f -c %T -- "$selected") == cgroup2fs ]]
    mount --make-rprivate /
    mount --bind "$selected" /sys/fs/cgroup
    mkdir -m 0700 /sys/fs/cgroup/supervisor
    # All tasks in this root belong to this wrapper's fresh transient unit.
    # Moving them beneath that same root satisfies the no-internal-process
    # rule without moving any existing desktop or service task.
    mapfile -t members < /sys/fs/cgroup/cgroup.procs
    [[ ${#members[@]} -gt 0 && ${#members[@]} -le 512 ]]
    for member in "${members[@]}"; do
        [[ $member =~ ^[1-9][0-9]*$ ]]
        printf '%s\n' "$member" > /sys/fs/cgroup/supervisor/cgroup.procs
    done
    [[ -z $(cat /sys/fs/cgroup/cgroup.procs) ]]
    printf '+memory +pids +cpu\n' > /sys/fs/cgroup/cgroup.subtree_control
    exec "$@"
fi
if [[ ${1:-} == --scope ]]; then
    shift
    unit=${1:?Fresh transient unit is required}
    shift
    [[ $unit == ure-rescue-test-*.scope && $# -gt 0 ]]
    selected=$(systemctl --user show "$unit" -p ControlGroup --value)
    [[ $selected == /user.slice/*/"$unit" ]]
    [[ $(systemctl --user show "$unit" -p Delegate --value) == yes ]]
    exec unshare --user --map-root-user --mount --cgroup -- \
        bash "$component/tests/with-rescue-cgroup.sh" --namespace "/sys/fs/cgroup$selected" "$@"
fi
[[ $# -gt 0 ]]
unit="ure-rescue-test-$(cat /proc/sys/kernel/random/uuid).scope"
exec systemd-run --user --scope --collect --quiet --unit "$unit" \
    --property 'Delegate=cpu memory pids' --property MemoryMax=1G --property MemorySwapMax=0 \
    --property TasksMax=512 --property CPUQuota=400% \
    bash "$component/tests/with-rescue-cgroup.sh" --scope "$unit" "$@"
