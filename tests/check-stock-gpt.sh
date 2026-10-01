#!/usr/bin/env bash
# Pinned OEM metadata on disposable sparse images; no device or donor execution.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
inputs="$component/referances/firmware/global/derived/uke_global_images_OS3.0.303.0.WOZMIXM_16.0/images"
work=$(mktemp -d)
trap 'find "$work" -depth -mindepth 1 -delete; rmdir -- "$work"' EXIT
expect_error() {
    local code=$1; shift
    if "$binary" "$@" > "$work/error"; then echo 'Unexpected stock GPT success' >&2; exit 1; fi
    jq -e --arg code "$code" '.error.code==$code' "$work/error" >/dev/null
}
for lun in 0 1 2 3 4 5; do
    case $lun in
        0) capacity=34359738368;;
        1|2) capacity=16777216;;
        3) capacity=4194304;;
        4) capacity=2147483648;;
        5) capacity=134217728;;
    esac
    preview="$work/preview $lun"; image="$work/lun$lun.img"
    "$binary" gpt stock-preview "$inputs" --capacity-bytes "$capacity" --lun "$lun" --profile global-os3.0.303.0 --output "$preview" > "$work/result"
    jq -e '.data.template_preview_only and (.data.restore_authorized==false) and .data.desired_table.healthy and (.data.regions|length)==5' "$work/result" >/dev/null
    truncate -s "$capacity" "$image"
    while IFS=$'\t' read -r name offset; do
        dd if="$preview/$name.bin" of="$image" bs=4096 seek="$((offset/4096))" conv=notrunc status=none
    done < <(jq -r '.regions[] | [.name,.offset] | @tsv' "$preview/manifest.json")
    "$binary" gpt backup --image "$image" --profile global-os3.0.303.0 --output "$work/original$lun" >/dev/null
    printf '\xff\xff\xff\xff' | dd of="$image" bs=1 seek=4112 conv=notrunc status=none
    expect_error identity-unavailable gpt stock-plan "$inputs" --image "$image" --lun "$lun" --profile global-os3.0.303.0 --output "$work/rejected"
    [[ ! -e $work/rejected ]]
    "$binary" gpt stock-plan "$inputs" --image "$image" --lun "$lun" --profile global-os3.0.303.0 --identity-backup "$work/original$lun" --output "$work/plan$lun" > "$work/result"
    jq -e '.data.operation=="gpt.stock" and (.data.stock_source.template_preview_only==false) and (.data.restores_partition_contents==false) and (.data.live_write_backend_ready==false)' "$work/result" >/dev/null
    confirm=$(jq -r .plan_sha256 "$work/plan$lun")
    expect_error confirmation-required gpt execute "$work/plan$lun" --image "$image" --journal "$work/rejected" --confirm bad
    "$binary" gpt execute "$work/plan$lun" --image "$image" --journal "$work/journal$lun" --confirm "$confirm" | jq -e '.data.state=="COMMITTED" and .data.completed_ranges[0]=="backup_table"' >/dev/null
    "$binary" gpt journal-inspect "$work/journal$lun" --image "$image" | jq -e '.data.classification=="TARGET_CONTENT_VERIFIED" and .data.current_table.healthy' >/dev/null
    "$binary" gpt rollback "$work/journal$lun" --image "$image" --confirm "$confirm" | jq -e '.data.state=="ROLLED_BACK"' >/dev/null
    "$binary" gpt journal-inspect "$work/journal$lun" --image "$image" | jq -e '.data.classification=="ORIGINAL_CONTENT_VERIFIED"' >/dev/null
done
expect_error wrong-profile gpt stock-preview "$inputs" --capacity-bytes 34359738368 --lun 0 --profile cn-os3.0.302.0 --output "$work/rejected"
expect_error invalid-lun gpt stock-preview "$inputs" --capacity-bytes 34359738368 --lun 6 --profile global-os3.0.303.0 --output "$work/rejected"
expect_error invalid-size gpt stock-preview "$inputs" --capacity-bytes 34359738369 --lun 0 --profile global-os3.0.303.0 --output "$work/rejected"
expect_error invalid-options gpt stock-preview "$inputs" --capacity-bytes 34359738368 --lun 0 --profile global-os3.0.303.0 --image "$work/lun0.img" --output "$work/rejected"
expect_error invalid-options gpt inspect --image "$work/lun0.img" --lun 0
expect_error invalid-options gpt inspect --image "$work/lun0.img" --identity-backup "$work/original0"
mkdir "$work/mismatched inputs"
while IFS= read -r name; do cp -- "$inputs/$name" "$work/mismatched inputs/$name"; done < <(jq -r '.inputs[] | select(.lun==0) | .filename' "$component/manifests/stock-gpt-global.json")
printf X | dd of="$work/mismatched inputs/patch0.xml" bs=1 seek=0 conv=notrunc status=none
expect_error stock-source-mismatch gpt stock-preview "$work/mismatched inputs" --capacity-bytes 34359738368 --lun 0 --profile global-os3.0.303.0 --output "$work/rejected"
[[ ! -e $work/rejected ]]
printf 'Stock GPT CLI: six capacity-derived LUNs, original-backup identity recovery, ordered metadata execution/readback/rollback, profile/capacity/LUN/context and OEM-hash refusals passed; sparse images only.\n'
