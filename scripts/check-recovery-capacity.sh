#!/usr/bin/env bash
# Read-only header, AVB and both-slot capacity gate against reviewed OEM profiles.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
image=$(realpath -- "${1:?Usage: check-recovery-capacity.sh IMAGE REPORT [PROFILE_DIRECTORY]}")
report=${2:?Report required}
profiles=${3:-"$component/manifests"}
[[ -f $image && ! -L $image ]]
bytes=$(stat -c %s "$image")
[[ $bytes -ge 8192 ]]
read_integer() {
    local offset=$1 count=$2 order=$3 value=0 byte shift=0
    local -a octets
    read -ra octets <<<"$(od -An -v -tu1 -j "$offset" -N "$count" "$image")"
    [[ ${#octets[@]} == "$count" ]]
    for byte in "${octets[@]}"; do
        if [[ $order == be ]]; then value=$((value * 256 + byte));
        else value=$((value + (byte << shift))); shift=$((shift + 8)); fi
    done
    [[ $value -ge 0 ]] || return 1
    printf '%s\n' "$value"
}
[[ $(dd if="$image" bs=1 count=8 status=none) == 'ANDROID!' ]]
kernel=$(read_integer 8 4 le); ramdisk=$(read_integer 12 4 le)
header=$(read_integer 20 4 le); version=$(read_integer 40 4 le)
signature=$(read_integer 1580 4 le)
[[ $kernel == 0 && $ramdisk -gt 0 && $header == 1584 && $version == 4 ]]
aligned_payload=$((4096 + ((ramdisk + 4095) / 4096) * 4096 + ((signature + 4095) / 4096) * 4096))
footer=$((bytes - 64))
[[ $(dd if="$image" bs=1 skip="$footer" count=4 status=none) == AVBf ]]
[[ $(read_integer "$((footer + 4))" 4 be) == 1 && $(read_integer "$((footer + 8))" 4 be) == 0 ]]
original=$(read_integer "$((footer + 12))" 8 be)
vbmeta_offset=$(read_integer "$((footer + 20))" 8 be)
vbmeta_bytes=$(read_integer "$((footer + 28))" 8 be)
[[ $original == "$aligned_payload" && $vbmeta_offset -ge $original && $vbmeta_bytes -gt 0 && $vbmeta_bytes -le 65536 ]]
[[ $((vbmeta_offset + vbmeta_bytes)) -le $footer ]]
[[ $(dd if="$image" bs=1 skip="$vbmeta_offset" count=4 status=none) == AVB0 ]]
board=$(awk '$1=="BOARD_RECOVERYIMAGE_PARTITION_SIZE" && $2==":=" {print $3}' "$component/src/device/xiaomi/uke/BoardConfig.mk")
[[ $board =~ ^[0-9]+$ ]]
work=$(mktemp -d "$component/build/capacity-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
: > "$work/profiles.jsonl"
for region in global cn; do
    profile="$profiles/stock-layout-$region.json"
    jq -e '.schema_version == 1 and (.source_archive_sha256|test("^[0-9a-f]{64}$")) and
        ([.entries[]|select(.label=="recovery_a" or .label=="recovery_b")]|length==2) and
        ([.entries[]|select(.label=="recovery_a" or .label=="recovery_b")|.label]|sort==["recovery_a","recovery_b"])' "$profile" >/dev/null
    for slot in recovery_a recovery_b; do
        entry=$(jq -ce --arg slot "$slot" '.entries[]|select(.label==$slot)' "$profile")
        jq -e '.state=="resolved" and .lun==4 and .sector_bytes==4096 and
            (.sector_count|type=="number") and .sector_count>0 and
            (.start_sector|test("^[0-9]+$"))' <<<"$entry" >/dev/null
        capacity=$(jq -r '.sector_count*.sector_bytes' <<<"$entry")
        [[ $capacity == "$board" && $bytes -le $capacity && $aligned_payload -le $((capacity - 69632)) ]]
    done
    jq -c --argjson image "$bytes" --argjson payload "$aligned_payload" --argjson board "$board" \
        '{profile,source_archive_sha256,profile_sha256:null,slots:[.entries[]|
            select(.label=="recovery_a" or .label=="recovery_b")|
            {slot:.label,lun,start_sector,bytes:(.sector_count*.sector_bytes),image_fits:($image<=(.sector_count*.sector_bytes)),
             board_size_matches:($board==(.sector_count*.sector_bytes)),
             payload_headroom_with_maximum_avb_bytes:(.sector_count*.sector_bytes-69632-$payload)}]}' "$profile" | \
        jq -c --arg digest "$(sha256sum "$profile"|cut -d' ' -f1)" '.profile_sha256=$digest' >> "$work/profiles.jsonl"
done
jq -n --arg image "$(sha256sum "$image"|cut -d' ' -f1)" --arg script "$(sha256sum "${BASH_SOURCE[0]}"|cut -d' ' -f1)" \
    --argjson bytes "$bytes" --argjson ramdisk "$ramdisk" --argjson payload "$aligned_payload" \
    --argjson signature "$signature" --argjson offset "$vbmeta_offset" --argjson vbmeta "$vbmeta_bytes" \
    --slurpfile profiles "$work/profiles.jsonl" \
    '{schema_version:1,recovery_image_sha256:$image,checker_sha256:$script,image_bytes:$bytes,
      header:{version:4,kernel_bytes:0,ramdisk_bytes:$ramdisk,signature_bytes:$signature,aligned_payload_bytes:$payload},
      avb:{footer_verified:true,vbmeta_offset:$offset,vbmeta_bytes:$vbmeta},profiles:$profiles,
      validation:{both_slots_fit_both_reviewed_oem_profiles:true,live_partition_capacity:false,physical_device:false}}' > "$report"
echo 'Recovery header and AVB fit both 100 MiB slots in the reviewed Global and CN profiles.'
