#!/usr/bin/env bash
# Synthetic image fixtures only. No mounts or live-device writes.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
fixture="$component/build/ure-host/uke-gpt-tests"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
expect_error() {
    local code=$1; shift
    if "$binary" "$@" > "$work/error"; then echo 'Unexpected GPT success' >&2; exit 1; fi
    jq -e --arg code "$code" '.error.code==$code' "$work/error" >/dev/null
}
"$fixture" --fixture "$work/disk.img"
"$binary" gpt inspect --image "$work/disk.img" | jq -e '.data.healthy==true and .data.partitions[0].label=="uke🚀"' >/dev/null
"$binary" gpt backup --image "$work/disk.img" --profile fixture --output "$work/backup" >/dev/null
"$binary" gpt verify "$work/backup" | jq -e '.data.verified==true and .data.region_count==5' >/dev/null
"$binary" gpt compare "$work/backup" --image "$work/disk.img" --profile fixture | jq -e '.data.matches==true' >/dev/null
expect_error wrong-profile gpt compare "$work/backup" --image "$work/disk.img" --profile other
cp "$work/disk.img" "$work/other.img"
expect_error wrong-target gpt compare "$work/backup" --image "$work/other.img" --profile fixture
# Corrupt primary CRC while leaving the valid backup and usable data unchanged.
printf '\xff\xff\xff\xff' | dd of="$work/disk.img" bs=1 seek=4112 conv=notrunc status=none
broken=$(sha256sum "$work/disk.img" | cut -d' ' -f1)
"$binary" gpt repair-plan --image "$work/disk.img" --profile fixture --output "$work/plan" >/dev/null
confirmation=$(jq -r .plan_sha256 "$work/plan")
expect_error confirmation-required gpt execute "$work/plan" --image "$work/disk.img" --journal "$work/rejected" --confirm bad
[[ ! -e $work/rejected ]]
"$binary" gpt execute "$work/plan" --image "$work/disk.img" --journal "$work/repair" --confirm "$confirmation" | jq -e '.data.state=="COMMITTED"' >/dev/null
"$binary" gpt inspect --image "$work/disk.img" | jq -e '.data.healthy==true' >/dev/null
"$binary" gpt journal-inspect "$work/repair" --image "$work/disk.img" | jq -e '.data.classification=="TARGET_CONTENT_VERIFIED" and (.data.recovery_actions|index("rollback"))!=null' >/dev/null
jq '.state="VERIFYING"' "$work/repair/journal.json" > "$work/staged-state"
cat "$work/staged-state" > "$work/repair/journal.json"
before_resume=$(sha256sum "$work/disk.img" | cut -d' ' -f1)
before_mtime=$(stat -c '%y %z' "$work/disk.img")
"$binary" gpt resume "$work/repair" --image "$work/disk.img" --confirm "$confirmation" | jq -e '.data.state=="COMMITTED" and .data.resume_readback_only==true' >/dev/null
[[ $before_resume == "$(sha256sum "$work/disk.img" | cut -d' ' -f1)" && $before_mtime == "$(stat -c '%y %z' "$work/disk.img")" ]]
expect_error unsafe-resume gpt resume "$work/repair" --image "$work/disk.img" --confirm "$confirmation"
"$binary" gpt rollback "$work/repair" --image "$work/disk.img" --confirm "$confirmation" | jq -e '.data.state=="ROLLED_BACK"' >/dev/null
[[ $broken == "$(sha256sum "$work/disk.img" | cut -d' ' -f1)" ]]
"$binary" gpt restore-plan "$work/backup" --image "$work/disk.img" --profile fixture --output "$work/restore-plan" >/dev/null
confirmation=$(jq -r .plan_sha256 "$work/restore-plan")
"$binary" gpt execute "$work/restore-plan" --image "$work/disk.img" --journal "$work/restore" --confirm "$confirmation" | jq -e '.data.completed_ranges[0]=="backup_table" and .data.completed_ranges[1]=="backup_header" and .data.state=="COMMITTED"' >/dev/null
cmp "$work/disk.img" "$work/other.img"
expect_error invalid-options gpt inspect --image "$work/disk.img" --object sysfs:fake
# QEMU prefixes /dev with the extracted ramdisk's empty dev directory. Use a
# synthetic FIFO to exercise the same non-regular rejection on host and target.
mkfifo "$work/non-regular"
expect_error invalid-image gpt inspect --image "$work/non-regular"
printf 'GPT CLI: backup/verify/compare, identity/profile/confirmation rejection, repair, inspected readback recovery, exact rollback and ordered restore passed; image fixtures only.\n'
