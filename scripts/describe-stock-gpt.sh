#!/usr/bin/env bash
# Host-only source catalog. Never executes OEM scripts or opens a block device.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
profile=${1:?Usage: describe-stock-gpt.sh PROFILE OUTPUT_JSON}
output=${2:?Missing output manifest}
[[ ! -e $output && ! -L $output ]]
entry=$(jq -ce --arg id "$profile" '.profiles[] | select(.id==$id and .download_status=="verified")' "$component/manifests/firmware.lock.json")
region=$(jq -r '.region | ascii_downcase' <<< "$entry")
url=$(jq -r '.url' <<< "$entry")
archive="$component/referances/firmware/$region/raw/${url##*/}"
source_hash=$(jq -er '.sha256' <<< "$entry")
[[ $source_hash =~ ^[0-9a-f]{64}$ && -f $archive && ! -L $archive ]]
[[ $(stat -c %s -- "$archive") == "$(jq -er '.measured_bytes' <<< "$entry")" ]]
[[ $(sha256sum -- "$archive" | cut -d' ' -f1) == "$source_hash" ]]
report="$component/reports/private/$profile-extraction.tsv"
[[ -f $report && ! -L $report && $(awk -F '\t' '$1=="source_sha256" {print $2}' "$report") == "$source_hash" ]]
work=$(mktemp -d)
trap 'find "$work" -depth -mindepth 1 -delete; rmdir -- "$work"' EXIT
rawprogram=()
for lun in 0 1 2 3 4 5; do
    for kind in gpt_main gpt_backup gpt_both rawprogram patch; do
        if [[ $kind == gpt_* ]]; then extension=bin; else extension=xml; fi
        name="$kind$lun.$extension"
        row=$(awk -F '\t' -v name="/$name" 'length($1)>=length(name) && substr($1,length($1)-length(name)+1)==name {print; count++} END {if(count!=1)exit 1}' "$report")
        IFS=$'\t' read -r relative size expected <<< "$row"
        [[ $relative == referances/firmware/"$region"/derived/* && $relative != *'/../'* && $expected =~ ^[0-9a-f]{64}$ ]]
        file="$component/$relative"
        [[ -f $file && ! -L $file && $(stat -c %s -- "$file") == "$size" && $(sha256sum -- "$file" | cut -d' ' -f1) == "$expected" ]]
        if [[ $extension == xml ]]; then xmllint --nonet --noout "$file"; fi
        [[ $kind != rawprogram ]] || rawprogram+=("$file")
        jq -n --arg name "$name" --arg kind "$kind" --arg hash "$expected" --argjson lun "$lun" --argjson bytes "$size" \
            '{lun:$lun,kind:$kind,filename:$name,bytes:$bytes,sha256:$hash}' >> "$work/inputs.json"
    done
done
"$component/build/inventory/uke-partition-inventory" "${rawprogram[@]}" > "$work/rawprogram.tsv"
inventory="$component/manifests/stock-layout-$region.json"
[[ $(jq -er '.profile' "$inventory") == "$profile" && $(jq -er '.source_archive_sha256' "$inventory") == "$source_hash" ]]
jq -Rn '[inputs | split("\t") | select(.[0]!="lun") | {lun:(.[0]|tonumber),label:.[1],start_sector:.[2],sector_count:(.[3]|tonumber),sector_bytes:(.[4]|tonumber),filename:.[5],state:.[6]}] | sort_by(.lun,.label,.start_sector)' \
    "$work/rawprogram.tsv" > "$work/current-inventory.json"
jq '.entries | sort_by(.lun,.label,.start_sector)' "$inventory" > "$work/expected-inventory.json"
cmp -- "$work/current-inventory.json" "$work/expected-inventory.json"
jq -n --arg profile "$profile" --arg archive "${url##*/}" --arg hash "$source_hash" \
    --arg version "$(jq -er '.version' <<< "$entry")" --arg inventory_hash "$(sha256sum -- "$inventory" | cut -d' ' -f1)" \
    --arg extraction_hash "$(sha256sum -- "$report" | cut -d' ' -f1)" --slurpfile inputs "$work/inputs.json" \
    '{schema:1,device:"uke",firmware_profile:$profile,firmware_version:$version,source_archive:{filename:$archive,sha256:$hash},logical_sector_bytes:4096,luns:[0,1,2,3,4,5],inputs:$inputs,inventory_sha256:$inventory_hash,private_extraction_record_sha256:$extraction_hash,geometry_policy:"Resolve terminal partitions and GPT locations from measured per-LUN capacity using reviewed OEM patch semantics; template values are not device geometry",validation:{source_archive:true,extracted_file_hashes:true,xml_syntax:true,rawprogram_range_inventory:true,inventory_comparison:true,stock_reconstruction:false,restore_authorized:false,physical_device:false}}' \
    > "$work/manifest.json"
[[ $(jq '.inputs|length' "$work/manifest.json") == 30 ]]
(set -o noclobber; cat -- "$work/manifest.json" > "$output")
printf 'Verified six-LUN stock GPT source catalog written; no reconstruction or write authorization.\n'
