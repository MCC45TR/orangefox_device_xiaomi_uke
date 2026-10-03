#!/usr/bin/env bash
# Host-only exact OEM source acquisition. Never executes an OEM flash script.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
profile=${1:?Usage: describe-stock-payloads.sh PROFILE NEW_PUBLIC_CATALOG}
output=${2:?Provide a new catalog path}
[[ ! -e $output && ! -L $output ]]
case "$profile" in global-os3.0.303.0|cn-os3.0.302.0) ;; *) echo 'No reviewed fastboot source profile' >&2; exit 2;; esac
entry=$(jq -ce --arg profile "$profile" '.profiles[] | select(.id==$profile and .download_status=="verified")' "$component/manifests/firmware.lock.json")
region=$(jq -r '.region | ascii_downcase' <<< "$entry")
url=$(jq -er .url <<< "$entry")
archive="$component/referances/firmware/$region/raw/${url##*/}"
expected=$(jq -er .sha256 <<< "$entry")
[[ -f $archive && ! -L $archive && $(stat -c %s "$archive") == "$(jq -er .measured_bytes <<< "$entry")" && $(sha256sum "$archive" | cut -d' ' -f1) == "$expected" ]]
mkdir -p "$component/build" "$component/reports/private"
work=$(mktemp -d "$component/build/stock-payload-catalog-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
tar -tzf "$archive" > "$work/index"
awk '/(^\/|(^|\/)\.\.?\/)/ {bad=1} END {exit bad}' "$work/index"
mapfile -t members < <(rg '/images/(boot|init_boot|vendor_boot|recovery|dtbo|vbmeta|vbmeta_system|super|metadata|userdata)\.img$|/images/(gpt_(main|backup|both)[0-5]\.bin|rawprogram[0-5]\.xml|patch[0-5]\.xml)$' "$work/index")
[[ ${#members[@]} == 40 ]]
destination="$component/referances/firmware/$region/derived/stock-payloads-$profile"
[[ ! -e $destination && ! -L $destination ]]
mkdir -p "$destination"
tar -xzf "$archive" -C "$destination" --no-same-owner --no-same-permissions --no-overwrite-dir -- "${members[@]}"
report="$component/reports/private/stock-payloads-$profile-extraction.tsv"
printf 'source_sha256\t%s\ncommand\t%s\n' "$expected" 'describe-stock-payloads.sh' > "$report"
for member in "${members[@]}"; do
    file="$destination/$member"
    [[ -f $file && ! -L $file && $(stat -c %h "$file") == 1 ]]
    bytes=$(stat -c %s "$file")
    hash=$(sha256sum "$file" | cut -d' ' -f1)
    printf '%s\t%s\t%s\n' "${file#"$component/"}" "$bytes" "$hash" >> "$report"
    [[ $member == *.img ]] || continue
    format=raw
    expanded=$bytes
    magic=$(od -An -tu4 -N4 "$file" | tr -d ' ')
    if [[ $magic == 3978755898 ]]; then
        format=android-sparse-v1
        major=$(od -An -tu2 -j4 -N2 "$file" | tr -d ' ')
        block=$(od -An -tu4 -j12 -N4 "$file" | tr -d ' ')
        blocks=$(od -An -tu4 -j16 -N4 "$file" | tr -d ' ')
        [[ $major == 1 && $block -ge 512 && $block -le 1048576 && $((block%512)) == 0 && $blocks -gt 0 ]]
        expanded=$((block*blocks))
    fi
    [[ $bytes -gt 0 && $expanded -gt 0 && $expanded -le 549755813888 && $((expanded%4096)) == 0 ]]
    jq -n --arg filename "${member##*/}" --arg hash "$hash" --arg format "$format" --argjson bytes "$bytes" --argjson expanded "$expanded" \
        '{filename:$filename,source_bytes:$bytes,source_sha256:$hash,encoding:$format,expanded_bytes:$expanded}' >> "$work/payloads.json"
done
jq -n --arg profile "$profile" --arg version "$(jq -er .version <<< "$entry")" --arg archive "${url##*/}" --arg hash "$expected" \
    --slurpfile payloads "$work/payloads.json" \
    '{schema:1,format:"ure-stock-payload-catalog",firmware_profile:$profile,firmware_version:$version,source_archive:{filename:$archive,sha256:$hash},logical_sector_bytes:4096,payloads:($payloads|sort_by(.filename)),
      policy:{early_firmware_preserved:true,unit_bound_ranges_preserved:true,image_length_is_not_partition_capacity:true,sparse_holes_require_explicit_zero_policy:true},
      validation:{archive_verified:true,selected_file_hashes:true,header_inventory:true,full_sparse_decoder:false,model_verified:false,sku_verified:false,physical_device:false,live_write_authorized:false}}' > "$work/catalog.json"
[[ $(jq '.payloads | length' "$work/catalog.json") == 10 ]]
(set -o noclobber; cat "$work/catalog.json" > "$output")
printf '%s\n' 'Ten stock payloads and thirty GPT/XML inputs acquired from the verified OEM archive; no model, SKU or live-write acceptance.'
