#!/usr/bin/env bash
# Signature samples and GPT gaps on a disposable image, without mounts or writes.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
fixture="$component/build/ure-host/uke-partition-tests"
work=$(mktemp -d)
trap 'find "$work" -depth -mindepth 1 -delete; rmdir -- "$work"' EXIT
"$fixture" --fixture "$work/disk.img"
before=$(sha256sum "$work/disk.img" | cut -d' ' -f1)
metadata=$(stat -c '%y %z %a' "$work/disk.img")
"$binary" gpt map --image "$work/disk.img" --output "$work/map.json" > "$work/result"
jq -e '.data.format=="ure-partition-map" and .data.read_only and .data.private_record and (.data.physical_test_record==false) and
    .data.unallocated_bytes==761856 and (.data.reserved_records|length)==1 and (.data.partitions|length)==3 and .data.signature_samples==3 and
    .data.partitions[0].content.type=="ntfs" and .data.partitions[1].content.type=="btrfs" and .data.partitions[2].content.encryption=="LUKS" and
    (.data.partitions[0].ownership_verified==false) and (.data.android_fbe_access_authorized==false) and (.data.management_eligible==false)' "$work/result" >/dev/null
cmp <(jq -S '.data' "$work/result") <(jq -S '.' "$work/map.json")
[[ $(stat -c %a "$work/map.json") == 600 ]]
"$binary" gpt map --image "$work/disk.img" --sector-size 512 > "$work/unavailable"
jq -e '(.data.unallocated_ranges_available==false) and .data.unallocated_bytes==null and .data.signature_samples==0 and (.data.warnings|length)>0' "$work/unavailable" >/dev/null
if "$binary" gpt map --image "$work/disk.img" --profile global-os3.0.303.0 > "$work/error"; then echo 'Profile accepted for a read-only partition map' >&2; exit 1; fi
jq -e '.error.code=="invalid-options"' "$work/error" >/dev/null
[[ $before == "$(sha256sum "$work/disk.img" | cut -d' ' -f1)" && $metadata == "$(stat -c '%y %z %a' "$work/disk.img")" ]]
printf 'Partition-map CLI: private export, offset NTFS/Btrfs/LUKS signatures, OEM reservation/gap accounting, unavailable geometry, context refusal and unchanged image passed.\n'
