#!/usr/bin/env bash
# Full filesystem/GPT job on a disposable image; never select a block device.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
fixture="$component/build/ure-host/uke-partition-job-tests"
work=$(mktemp -d)
trap 'find "$work" -depth -mindepth 1 -delete; rmdir -- "$work"' EXIT
expect_error() {
    local code=$1; shift
    if "$binary" "$@" > "$work/error.json"; then echo 'Unexpected partition job success' >&2; exit 1; fi
    jq -e --arg code "$code" '.error.code==$code' "$work/error.json" >/dev/null
}
"$fixture" --fixture "$work/disk.img"
"$fixture" --request > "$work/request.json"
before=$(sha256sum "$work/disk.img" | cut -d' ' -f1)
"$binary" partition job-plan "$work/request.json" --image "$work/disk.img" --sector-size 4096 --profile fixture --output "$work/plan.json" > "$work/result.json"
jq -e '.data.operation=="partition.apply-layout" and .data.formats_filesystems and .data.preserves_userdata_files and
    .data.execution_scope=="FILESYSTEMS_AND_GPT_WITHIN_ORIGINAL_USERDATA" and .data.interrupt_scenario=="FORCED_REBOOT" and
    (.data.live_write_backend_ready==false) and (.data.physical_test_record==false)' "$work/result.json" >/dev/null
[[ $(stat -c %a "$work/plan.json") == 600 && $before == "$(sha256sum "$work/disk.img" | cut -d' ' -f1)" ]]
confirmation=$(jq -r .plan_sha256 "$work/plan.json")
expect_error confirmation-required partition job-execute "$work/plan.json" --image "$work/disk.img" --sector-size 4096 --journal "$work/rejected" --confirm bad
[[ ! -e $work/rejected ]]
expect_error invalid-options partition job-plan "$work/request.json" --image "$work/disk.img" --profile fixture --output "$work/unused.json" --root "$work"
expect_error invalid-options partition job-plan "$work/request.json" --image "$work/disk.img" --object sysfs:fake --profile fixture --output "$work/unused.json"
jq '.mode="advanced" | .record_edits=[{index:1,contents:"format",filesystem:"ext4"}]' "$work/request.json" > "$work/oem-format.json"
expect_error advanced-content-workflow-required partition job-plan "$work/oem-format.json" --image "$work/disk.img" --sector-size 4096 --profile fixture --output "$work/unused.json"
"$binary" partition job-execute "$work/plan.json" --image "$work/disk.img" --sector-size 4096 --journal "$work/job" --confirm "$confirmation" > "$work/result.json"
jq -e '.data.state=="COMMITTED" and .data.verified and .data.complete_partition_job and .data.protected_ranges_verified' "$work/result.json" >/dev/null
"$binary" partition job-inspect "$work/job" --image "$work/disk.img" --sector-size 4096 > "$work/result.json"
jq -e '.data.classification=="TARGET" and .data.before_and_after_verified and (.data.recovery_actions|index("rollback"))!=null' "$work/result.json" >/dev/null
expect_error invalid-options partition job-inspect "$work/job" --image "$work/disk.img" --sector-size 4096 --confirm "$confirmation"
"$binary" partition job-rollback "$work/job" --image "$work/disk.img" --sector-size 4096 --confirm "$confirmation" > "$work/result.json"
jq -e '.data.state=="ROLLED_BACK" and .data.verified' "$work/result.json" >/dev/null
[[ $before == "$(sha256sum "$work/disk.img" | cut -d' ' -f1)" ]]
# Advanced front placement is a complete explicit erase/recreate operation.
jq '.mode="advanced" | .placement="before_userdata" | .userdata_policy="recreate"' "$work/request.json" > "$work/front.json"
"$binary" partition job-plan "$work/front.json" --image "$work/disk.img" --sector-size 4096 --profile fixture --output "$work/front-plan.json" > "$work/result.json"
jq -e '.data.risk=="ERASE_USERDATA_AND_CREATE_OS_FILESYSTEMS" and (.data.preserves_userdata_files==false) and
    .data.gpt.layout.rows[3].destroys_existing_data and (.data.android_userdata_boot_compatibility_verified==false)' "$work/result.json" >/dev/null
front_confirmation=$(jq -r .plan_sha256 "$work/front-plan.json")
"$binary" partition job-execute "$work/front-plan.json" --image "$work/disk.img" --sector-size 4096 --journal "$work/front-job" --confirm "$front_confirmation" > "$work/result.json"
jq -e '.data.state=="COMMITTED" and .data.complete_partition_job' "$work/result.json" >/dev/null
"$binary" partition job-rollback "$work/front-job" --image "$work/disk.img" --sector-size 4096 --confirm "$front_confirmation" > "$work/result.json"
[[ $before == "$(sha256sum "$work/disk.img" | cut -d' ' -f1)" ]]
printf 'Partition job CLI: saved-plan validation, exact confirmation/options, userdata-only content boundary, full filesystem/GPT commit and byte rollback, advanced front recreate and truthful Android/device scope passed.\n'
