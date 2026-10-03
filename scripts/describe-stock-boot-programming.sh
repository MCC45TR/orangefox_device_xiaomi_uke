#!/usr/bin/env bash
# Host-only canonical AOSP programming layouts from verified OEM files.
set -euo pipefail
umask 077
[[ $# == 2 ]] || { echo 'Usage: describe-stock-boot-programming.sh INPUTS NEW_CATALOG' >&2; exit 2; }
component=$(cd -- "$(dirname -- "$0")/.." && pwd)
inputs=$1
output=$2
[[ -d $inputs && ! -e $output && ! -L $output ]]
source_catalog="$component/manifests/stock-payloads-global.json"
upstream=1efa79514b2f520c20a837c9216ff6b6e7e0dda3
[[ $(git -C "$component/src/upstream/orangefox-android16/system/core" rev-parse HEAD) == "$upstream" ]]
mkdir -p "$component/build"
work=$(mktemp -d "$component/build/stock-programming-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
for name in boot dtbo init_boot recovery vendor_boot; do
    filename="$name.img"
    source="$inputs/$filename"
    row=$(jq -ce --arg file "$filename" '.payloads[] | select(.filename==$file and .encoding=="raw")' "$source_catalog")
    bytes=$(jq -er .source_bytes <<< "$row")
    expected=$(jq -er .source_sha256 <<< "$row")
    [[ -f $source && ! -L $source && $(stat -c %h "$source") == 1 && $(stat -c %s "$source") == "$bytes" && $(sha256sum "$source" | cut -d' ' -f1) == "$expected" ]]
    partition_bytes=$bytes
    layout=exact-source
    cp --reflink=auto -- "$source" "$work/$filename"
    if [[ $name == dtbo ]]; then
        [[ $bytes == 20971520 && $(od -An -tx1 -N4 -j "$((bytes-64))" "$source" | tr -d '[:space:]') == 41564266 ]]
        partition_bytes=25165824
        layout=aosp-fastboot-copy-avb-footer
        truncate -s "$partition_bytes" "$work/$filename"
        dd if="$source" of="$work/$filename" bs=1M iflag=skip_bytes,count_bytes oflag=seek_bytes \
            skip="$((bytes-64))" seek="$((partition_bytes-64))" count=64 conv=notrunc status=none
    fi
    partition_hash=$(sha256sum "$work/$filename" | cut -d' ' -f1)
    [[ $(sha256sum "$source" | cut -d' ' -f1) == "$expected" ]]
    jq -n --arg name "$name" --arg source_hash "$expected" --arg partition_hash "$partition_hash" \
        --arg layout "$layout" --argjson source_bytes "$bytes" --argjson partition_bytes "$partition_bytes" \
        '{name:$name,source_bytes:$source_bytes,source_sha256:$source_hash,partition_bytes:$partition_bytes,partition_sha256:$partition_hash,programming_layout:$layout}' >> "$work/rows.json"
done
jq -n --slurpfile source "$source_catalog" --slurpfile rows "$work/rows.json" --arg revision "$upstream" \
    '{schema:1,format:"ure-stock-boot-programming-catalog",firmware_profile:$source[0].firmware_profile,
      source_archive:$source[0].source_archive,programming_source:{project:"AOSP system/core",revision:$revision,function:"fastboot/copy_avb_footer"},
      images:$rows,validation:{source_files_verified:true,canonical_layouts_verified:true,physical_device:false,installed_firmware_verified:false,live_write_authorized:false}}' > "$output"
echo 'Five canonical boot programming layouts verified; no storage device was written.'
