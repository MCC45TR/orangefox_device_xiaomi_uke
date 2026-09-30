#!/usr/bin/env bash
# Bounded host-only unpacking of a concatenated vendor_boot DTB section.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
profile=${1:?Usage: inspect-stock-dtb.sh global|cn DTB_FILE}
input=${2:?Missing verified extracted DTB section}
case "$profile" in global|cn);; *) exit 2;; esac
[[ -f $input && ! -L $input ]] || exit 2
bytes=$(stat -c %s "$input")
[[ $bytes -ge 40 && $bytes -le 16777216 ]] || exit 2
dtc="$component/../senemos-uke-kernel/build/linux-7.2.8-arm64/scripts/dtc/dtc"
[[ -x $dtc ]] || { echo 'Build the pinned host dtc first' >&2; exit 1; }
destination="$component/build/dtb-$profile"
mkdir -p "$destination" "$component/reports/private"
report="$component/reports/private/$profile-dtb-inventory.tsv"
printf 'entry\toffset\tbytes\tsha256\n' > "$report"
offset=0
count=0
while [[ $offset -lt $bytes ]]; do
    [[ $count -lt 64 && $((bytes-offset)) -ge 40 ]] || { echo 'Invalid DTB count or trailing data' >&2; exit 1; }
    magic=$(od -An -tx4 --endian=big -j "$offset" -N4 "$input" | tr -d ' ')
    size=$(od -An -tu4 --endian=big -j "$((offset+4))" -N4 "$input" | tr -d ' ')
    [[ $magic == d00dfeed && $size =~ ^[0-9]+$ && $size -ge 40 && $size -le $((bytes-offset)) ]] || {
        echo 'Invalid FDT header or unrecognized trailer; inventory is incomplete' >&2; exit 1;
    }
    file="$destination/entry-$count.dtb"
    dd if="$input" of="$file" bs=1M iflag=skip_bytes,count_bytes skip="$offset" count="$size" status=none
    "$dtc" --quiet -I dtb -O dts -o "$destination/entry-$count.dts" "$file"
    printf '%s\t%s\t%s\t%s\n' "$count" "$offset" "$size" "$(sha256sum "$file" | cut -d' ' -f1)" >> "$report"
    offset=$((offset+size))
    count=$((count+1))
done
printf 'Package DTB inventory: %s complete FDT entries; no installed-device variant selected.\n' "$count"
