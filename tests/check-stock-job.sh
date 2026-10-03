#!/usr/bin/env bash
# Actual host CLI routes on six disposable regular images. No tablet writes.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$component"
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
inputs="$component/referances/firmware/global/derived/stock-payloads-global-os3.0.303.0/uke_global_images_OS3.0.303.0.WOZMIXM_16.0/images"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"$component/build/ure-host/uke-stock-job-tests" --fixture "$inputs" "$work/images" > "$work/fixture.json"
"$binary" stock image-inspect "$inputs/metadata.img" --json > "$work/sparse.json"
jq -e '.data | .encoding=="android-sparse-v1" and .expanded_bytes==67108864 and .structure_verified and .declared_checksums_verified and (.physical_test_record|not)' "$work/sparse.json" >/dev/null
jq '.payloads=[{lun:4,label:"dtbo_a",filename:"dtbo.img"},{lun:4,label:"vbmeta_a",filename:"vbmeta.img"},{lun:0,label:"vbmeta_system_a",filename:"vbmeta_system.img"}]' "$work/images/request.json" > "$work/selected.json"
"$binary" stock job-plan "$work/selected.json" --output "$work/plan.json" --json > "$work/review.json"
jq -e '.luns|length==6' "$work/plan.json" >/dev/null
jq -e '.regions|length==33' "$work/plan.json" >/dev/null
confirmation=$(jq -er '.plan_sha256' "$work/plan.json")
if "$binary" stock job-execute "$work/plan.json" --journal "$work/denied" --confirm wrong --json > "$work/refused.json"; then
    echo 'Stock CLI accepted an unreviewed confirmation' >&2
    exit 1
fi
jq -e '.error.code=="confirmation-required"' "$work/refused.json" >/dev/null
[[ ! -e $work/denied ]]
"$binary" stock job-execute "$work/plan.json" --journal "$work/job" --confirm "$confirmation" --json > "$work/committed.json"
jq -e '.data | .state=="COMMITTED" and .all_six_luns_verified and .protected_ranges_verified and (.atomic_all_luns|not)' "$work/committed.json" >/dev/null
"$binary" stock job-inspect "$work/job" --json > "$work/inspected.json"
jq -e '.data | .classification=="TARGET_CONTENT_VERIFIED" and .before_and_after_verified and .all_six_originals_verified' "$work/inspected.json" >/dev/null
if "$binary" stock job-inspect "$work/job" --confirm "$confirmation" --json > "$work/invalid.json"; then
    echo 'Read-only stock inspection accepted a confirmation option' >&2
    exit 1
fi
jq -e '.error.code=="invalid-options"' "$work/invalid.json" >/dev/null
"$binary" stock job-rollback "$work/job" --confirm "$confirmation" --json > "$work/rolled-back.json"
jq -e '.data | .state=="ROLLED_BACK" and .all_six_luns_verified and (.physical_test_record|not)' "$work/rolled-back.json" >/dev/null
if "$binary" stock job-resume "$work/job" --confirm "$confirmation" --json > "$work/terminal.json"; then
    echo 'Stock CLI resumed a rolled-back job' >&2
    exit 1
fi
jq -e '.error.code=="unsafe-recovery"' "$work/terminal.json" >/dev/null
printf '%s\n' 'Stock CLI source inspection, six-LUN review/execute/inspect/rollback and option/confirmation guards passed; regular-image scope only.'
