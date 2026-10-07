#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Small host text controls for filename-bound metadata comparisons.
set -euo pipefail
export LC_ALL=C LANG=C
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
source "$component/tests/f2fs-metadata-oracle-lib.sh"
work=$(mktemp -d "$component/build/f2fs-metadata-controls-XXXXXXXX")
write_log() {
    local file=$1 name=$2 size=$3
    printf 'i_name [%s]\ni_mode [0x 8180 : 33152]\ni_uid [0x 3e8 : 1000]\ni_gid [0x 3e8 : 1000]\ni_links [0x 1 : 1]\ni_size [0x 0 : %s]\n' "$name" "$size" > "$file"
}
write_log "$work/details.log" details.txt 35
write_log "$work/payload.log" payload.bin 83886080
ure_f2fs_fixture_metadata "$work/details.log" "$work/payload.log" > "$work/baseline.json"
ure_f2fs_fixture_metadata "$work/payload.log" "$work/details.log" > "$work/reordered.json"
cmp "$work/baseline.json" "$work/reordered.json"
jq -e '.[0].name=="details.txt" and .[0].size==35 and .[1].name=="payload.bin" and .[1].size==83886080' "$work/baseline.json" >/dev/null
detected=0
for field in mode uid gid links size; do
    awk -v field="i_$field" '$1==field {sub(/1000\]/,"1001]");sub(/33152\]/,"33153]");sub(/1\]/,"2]");sub(/35\]/,"36]")} {print}' "$work/details.log" > "$work/changed.log"
    ure_f2fs_fixture_metadata "$work/changed.log" "$work/payload.log" > "$work/changed-$field.json"
    if cmp -s "$work/baseline.json" "$work/changed-$field.json"; then echo 'Metadata drift was missed' >&2; exit 1; fi
    detected=$((detected+1))
done
write_log "$work/details-swapped.log" details.txt 83886080
write_log "$work/payload-swapped.log" payload.bin 35
ure_f2fs_fixture_metadata "$work/details-swapped.log" "$work/payload-swapped.log" > "$work/swapped.json"
if cmp -s "$work/baseline.json" "$work/swapped.json"; then echo 'Per-file size swapping was missed' >&2; exit 1; fi
detected=$((detected+1))
refused=0
for kind in missing duplicate unknown duplicate-name; do
    case "$kind" in
        missing) sed '/^i_name /d' "$work/details.log" > "$work/invalid.log";;
        duplicate) cat "$work/details.log" > "$work/invalid.log"; printf 'i_uid [0x 3e8 : 1000]\n' >> "$work/invalid.log";;
        unknown) sed 's/\[details.txt\]/[unexpected.txt]/' "$work/details.log" > "$work/invalid.log";;
        duplicate-name) sed 's/\[details.txt\]/[payload.bin]/' "$work/details.log" > "$work/invalid.log";;
    esac
    if ure_f2fs_fixture_metadata "$work/invalid.log" "$work/payload.log" > "$work/invalid-$kind.json" 2> "$work/invalid-$kind.stderr"; then
        echo 'Incomplete or ambiguous metadata passed' >&2; exit 1
    fi
    refused=$((refused+1))
done
[[ $detected == 6 && $refused == 4 ]]
jq -n '{passed:true,evidence_class:"host-text-f2fs-metadata-oracle",order_controls:2,metadata_drift_controls:6,malformed_refusals:4,
    keyed_by_filename:true,tablet_writes:false,filesystem_image_modified:false}' > "$work/verification.json"
printf 'PASS: F2FS metadata order, six drift and four malformed controls; private results retained.\n'
