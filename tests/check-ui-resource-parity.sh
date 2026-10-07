#!/usr/bin/env bash
# Actual pinned mkbootfs conversion, UI projections and independent mode gate.
set -euo pipefail
umask 077
candidate=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
component=${UKE_RECOVERY_SOURCE_COMPONENT:-$candidate}
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "${BASH_SOURCE[0]}"
fi
work=$(mktemp -d "$component/build/ui-resource-parity-fixture-XXXXXX")
output="$work/output"
staging="$output/target/product/uke/recovery/root"
tools="$component/src/upstream/orangefox-android16/out-public/host/linux-x86/bin"
mkdir -p "$output/host/linux-x86/"{bin,lib64} "$output/target/product/uke/system" \
    "$staging/twres/"{languages,fonts,images,pages} "$staging/sbin" "$staging/system/etc/ure/licenses"
cp -p "$tools/mkbootfs" "$tools/lz4" "$output/host/linux-x86/bin/"
cp -p "$tools/../lib64/libc++.so" "$output/host/linux-x86/lib64/"
for path in twres/languages/en.xml twres/fonts/font.ttf twres/fonts/LICENSE.txt twres/images/icon.png \
    twres/pages/main.xml twres/ui.xml sbin/maintainer.xml system/etc/ure/licenses/font.txt; do
    printf 'GUI resource fixture; no licensed font or rendering claim.\n' > "$staging/$path"
done
"$tools/mkbootfs" -d "$output/target/product/uke/system" "$staging" > "$work/canonical.cpio"
"$tools/lz4" -l -12 --favor-decSpeed < "$work/canonical.cpio" > "$work/ramdisk.lz4"
mkdir "$work/extracted"
bwrap --ro-bind / / --bind "$work/extracted" /mnt --chdir /mnt cpio -idm --quiet --no-absolute-filenames < "$work/canonical.cpio"
[[ $(stat -c %a "$staging/twres/fonts/font.ttf") == 600 && $(stat -c %a "$work/extracted/twres/fonts/font.ttf") == 644 ]]
image="$work/recovery.img"
truncate -s 104857600 "$image"
printf 'ANDROID!' | dd of="$image" bs=1 conv=notrunc status=none
word() {
    local offset=$1 value=$2 encoded='' byte shift
    for shift in 0 8 16 24; do printf -v byte '\\%03o' "$(((value>>shift)&255))"; encoded+="$byte"; done
    printf '%b' "$encoded" | dd of="$image" bs=1 seek="$offset" conv=notrunc status=none
}
word 12 "$(stat -c %s "$work/ramdisk.lz4")"
word 40 4
dd if="$work/ramdisk.lz4" of="$image" bs=1M oflag=seek_bytes seek=4096 conv=notrunc status=none
bash "$component/scripts/check-packed-payload.sh" "$output" "$image" "$work/extracted" "$work/packed-positive"
helper="$candidate/scripts/check-ui-resource-parity.sh"
bash "$helper" "$staging" "$work/extracted" "$work/ui-positive" > "$work/ui-positive.json"
jq -e '.staged_ui_assets_sha256!=.extracted_ui_assets_sha256 and .validation.file_bytes_match==true and
    .validation.members_types_links_match==true and .validation.mode_aware_inventories_retained==true and
    .validation.canonical_packed_modes_verified==false' "$work/ui-positive.json" >/dev/null
cases=0
refuse() {
    local name=$1
    if bash "$helper" "$staging" "$work/$name" "$work/check-$name" > "$work/$name.log" 2>&1; then
        printf 'Unexpected UI comparison acceptance: %s\n' "$name" >&2; exit 1
    fi
    ((cases+=1)); printf 'REFUSED %s\n' "$name"
}
for name in content missing extra hidden type link fifo root-link; do cp -a "$work/extracted" "$work/$name"; done
printf altered > "$work/content/twres/fonts/font.ttf"; refuse content
rm "$work/missing/twres/fonts/font.ttf"; refuse missing
printf extra > "$work/extra/twres/extra"; refuse extra
mkdir "$work/hidden/twres/.git"; printf extra > "$work/hidden/twres/.git/resource"; refuse hidden
rm "$work/type/twres/fonts/font.ttf"; mkdir "$work/type/twres/fonts/font.ttf"; refuse type
rm "$work/link/twres/fonts/font.ttf"; ln -s "$work/extracted/twres/fonts/font.ttf" "$work/link/twres/fonts/font.ttf"; refuse link
mkfifo "$work/fifo/twres/fifo"; refuse fifo
mv "$work/root-link/system/etc/ure/licenses" "$work/licenses"
ln -s "$work/licenses" "$work/root-link/system/etc/ure/licenses"; refuse root-link
cp -a "$work/extracted" "$work/mode"
chmod 600 "$work/mode/twres/fonts/font.ttf"
bash "$helper" "$staging" "$work/mode" "$work/ui-mode" > "$work/ui-mode.json"
if bash "$component/scripts/check-packed-payload.sh" "$output" "$image" "$work/mode" "$work/packed-mode" > "$work/mode.log" 2>&1; then
    echo 'Canonical package gate accepted a changed packed mode' >&2; exit 1
fi
((cases+=1)); printf 'REFUSED packed-mode-through-canonical-gate\n'
jq -n --arg helper "$(sha256sum "$helper"|cut -d' ' -f1)" --argjson cases "$cases" \
    '{schema:1,helper_sha256:$helper,actual_mkbootfs_conversion:true,ui_parity_positive:true,
      preserved_mode_aware_identities:true,mutation_refusals:$cases,canonical_gate_rejects_changed_packed_mode:true,
      physical_device:false}' > "$work/verification.json"
printf '%s\n' "$work" > "$component/reports/private/ui-resource-parity-test-latest-job"
printf 'PASS actual UI permission conversion and %s composed refusal controls.\n' "$cases"
