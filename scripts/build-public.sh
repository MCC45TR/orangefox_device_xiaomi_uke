#!/usr/bin/env bash
# Bind build inputs to a non-personal path; keep logs in the ignored private area.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" arm64 bash "$component/scripts/build-public.sh" "$@"
fi
tree="$component/src/upstream/orangefox-android16"
jobs=${1:-16}
[[ $jobs =~ ^([1-9]|1[0-6])$ ]] || exit 2
ceiling=${UKE_HOST_JOBS:?The host budget runner must supply compile admission}
[[ $ceiling =~ ^([1-9]|1[0-6])$ ]] || exit 2
jobs=$(bash "$component/scripts/host-budget-policy.sh" --compile-jobs "$jobs" "$ceiling")
vm_fixture=${UKE_BUILD_VM_FIXTURE:-0}
[[ $vm_fixture == 0 || $vm_fixture == 1 ]] || exit 2
mkdir -p "$component/reports/private"
# Compilation must use the project files being reviewed and tested. Staging is
# explicit because prepare-build-tree also verifies the firmware and patch stack.
while IFS= read -r -d '' source_file; do
    staged_file="$tree/device/xiaomi/uke/${source_file#"$component/src/device/xiaomi/uke/"}"
    if ! cmp -s -- "$source_file" "$staged_file"; then
        echo 'Staged device sources differ; run prepare-build-tree.sh with the verified profile before building.' >&2
        exit 1
    fi
done < <(find "$component/src/device/xiaomi/uke" -type f -print0)
cmp -- "$component/src/device/xiaomi/uke/ure-gui.cpp" "$tree/bootable/recovery/gui/ure.cpp"
# OrangeFox captures shell exports during lunch for its vendor packaging script.
# A make-only FOX_BUILD_BASH value would still let that script copy its prebuilt.
bash "$component/scripts/with-android-build-environment.sh" "$tree" \
    bash -c 'source build/envsetup.sh >/dev/null && lunch twrp_uke-bp2a-eng >/dev/null && targets=(recoveryimage) && if [[ $2 == 1 ]]; then targets+=(uke-btrfs-vm-fixture-soong); fi && m "${targets[@]}" -j"$1"' bash "$jobs" "$vm_fixture" \
    >> "$component/reports/private/recoveryimage-neutral-build.log" 2>&1
echo 'Recovery build completed; run the complete payload and package audits next.'
