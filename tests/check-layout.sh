#!/usr/bin/env bash
# Synthetic whole-disk images only. No mounts, unlocks or device writes.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
fixture="$component/build/ure-host/uke-layout-tests"
work=$(mktemp -d)
trap 'find "$work" -depth -mindepth 1 -delete; rmdir -- "$work"' EXIT
expect_error() {
    local code=$1; shift
    if "$binary" "$@" > "$work/error.json"; then echo 'Unexpected layout success' >&2; exit 1; fi
    jq -e --arg code "$code" '.error.code==$code' "$work/error.json" >/dev/null
}
"$fixture" --fixture "$work/disk.img"
"$fixture" --request > "$work/request.json"
"$binary" gpt inspect --image "$work/disk.img" > "$work/original.json"
"$binary" gpt backup --image "$work/disk.img" --profile fixture --output "$work/original-gpt" >/dev/null
stamp=$(stat -c '%y %z %s' "$work/disk.img")
"$binary" gpt layout-preview "$work/request.json" --image "$work/disk.img" --profile fixture --output "$work/preview.json" > "$work/result.json"
jq -e '.data.pool.source=="ORIGINAL_USERDATA_ONLY" and .data.pool.bytes==4277141504 and
    .data.rows[0].role=="userdata" and .data.rows[0].start_lba==4096 and .data.rows[0].action=="VERIFIED_SHRINK_REQUIRED" and
    .data.rows[2].bytes==1710227456 and .data.rows[3].bytes==854589440 and
    (.data.complete_partition_job==false) and (.data.live_write_backend_ready==false)' "$work/result.json" >/dev/null
[[ $(stat -c %a "$work/preview.json") == 600 && $stamp == "$(stat -c '%y %z %s' "$work/disk.img")" ]]
# Resolved UUIDs bind repeat previews and the metadata plan to the same layout.
jq '.request' "$work/preview.json" > "$work/resolved.json"
"$binary" gpt layout-preview "$work/resolved.json" --image "$work/disk.img" --profile fixture --output "$work/repeated.json" >/dev/null
cmp "$work/preview.json" "$work/repeated.json"
"$binary" gpt layout-plan "$work/resolved.json" --image "$work/disk.img" --profile fixture --output "$work/plan.json" > "$work/result.json"
jq -e '.data.operation=="gpt.layout" and .data.desired_table.healthy and .data.execution_scope=="GPT_METADATA_ONLY" and
    (.data.formats_filesystems==false) and (.data.complete_partition_job==false)' "$work/result.json" >/dev/null
confirmation=$(jq -r .plan_sha256 "$work/plan.json")
expect_error confirmation-required gpt execute "$work/plan.json" --image "$work/disk.img" --journal "$work/rejected" --confirm bad
[[ ! -e $work/rejected ]]
expect_error invalid-options gpt layout-preview "$work/request.json" --image "$work/disk.img" --profile fixture --root "$work"
expect_error invalid-options gpt layout-preview "$work/request.json" --image "$work/disk.img" --object sysfs:fake --profile fixture
jq '.placement="before_userdata"' "$work/request.json" > "$work/front.json"
expect_error userdata-migration-required gpt layout-plan "$work/front.json" --image "$work/disk.img" --profile fixture --output "$work/front-plan.json"
jq '.mode="advanced" | .userdata_policy="recreate"' "$work/front.json" > "$work/front-advanced.json"
"$binary" gpt layout-plan "$work/front-advanced.json" --image "$work/disk.img" --profile fixture --output "$work/front-plan.json" > "$work/result.json"
jq -e '.data.layout.rows[3].destroys_existing_data and .data.layout.rows[3].action=="ERASE_AND_RECREATE_REQUIRED" and
    .data.layout.rows[3].start_lba>4096 and (.data.formats_filesystems==false)' "$work/result.json" >/dev/null
jq '.record_edits=[{index:1,partuuid:"11111111-2222-4333-8444-555555555555",contents:"preserve"}]' "$work/request.json" > "$work/edit.json"
expect_error advanced-mode-required gpt layout-plan "$work/edit.json" --image "$work/disk.img" --profile fixture --output "$work/edit-plan.json"
jq '.mode="advanced"' "$work/edit.json" > "$work/expert.json"
"$binary" gpt layout-plan "$work/expert.json" --image "$work/disk.img" --profile fixture --output "$work/edit-plan.json" > "$work/result.json"
jq -e '.data.desired_table.partitions[0].partuuid=="11111111-2222-4333-8444-555555555555" and
    .data.desired_table.partitions[0].start_lba==256 and .data.desired_table.partitions[0].end_lba==4095' "$work/result.json" >/dev/null
[[ $stamp == "$(stat -c '%y %z %s' "$work/disk.img")" ]]
"$binary" gpt execute "$work/plan.json" --image "$work/disk.img" --journal "$work/journal" --confirm "$confirmation" > "$work/result.json"
jq -e '.data.state=="COMMITTED" and .data.execution_scope=="GPT_METADATA_ONLY" and (.data.complete_partition_job==false)' "$work/result.json" >/dev/null
"$binary" gpt journal-inspect "$work/journal" --image "$work/disk.img" | jq -e '.data.classification=="TARGET_CONTENT_VERIFIED" and (.data.recovery_actions|index("rollback"))!=null' >/dev/null
"$binary" gpt rollback "$work/journal" --image "$work/disk.img" --confirm "$confirmation" | jq -e '.data.state=="ROLLED_BACK"' >/dev/null
"$binary" gpt inspect --image "$work/disk.img" > "$work/restored.json"
cmp <(jq .data "$work/original.json") <(jq .data "$work/restored.json")
"$binary" gpt backup --image "$work/disk.img" --profile fixture --output "$work/restored-gpt" >/dev/null
for region in primary_table primary_header backup_table backup_header protective_mbr; do
    cmp "$work/original-gpt/$region.bin" "$work/restored-gpt/$region.bin"
done
printf 'Layout CLI: original-userdata bounds, exact units, deterministic private previews, advanced policy, front erase/recreate warnings, image GPT commit/readback and exact rollback passed; no filesystem or device writes.\n'
