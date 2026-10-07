#!/usr/bin/env bash
# Actual pinned mkbootfs/lz4 controls. Host fixtures only; no tablet access.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "$0"
fi
tools="$component/src/upstream/orangefox-android16/out-public/host/linux-x86/bin"
[[ -x $tools/mkbootfs && -x $tools/lz4 ]]
work=$(mktemp -d "$component/build/packed-payload-fixture-XXXXXX")
output="$work/output"
staging="$output/target/product/uke/recovery/root"
mkdir -p "$output/host/linux-x86/bin" "$output/host/linux-x86/lib64" "$output/target/product/uke/system" "$staging/system/bin" "$staging/data"
cp -p -- "$tools/mkbootfs" "$tools/lz4" "$output/host/linux-x86/bin/"
cp -p -- "$tools/../lib64/libc++.so" "$output/host/linux-x86/lib64/"
printf 'Source-built packaging fixture\n' > "$staging/system/bin/demo"
chmod 700 "$staging/system/bin/demo" "$staging/data"
ln -s /system/bin/demo "$staging/init"
"$tools/mkbootfs" -d "$output/target/product/uke/system" "$staging" > "$work/original.cpio"
"$tools/lz4" -l -12 --favor-decSpeed < "$work/original.cpio" > "$work/original.lz4"
image="$work/recovery.img"
truncate -s 104857600 "$image"
printf 'ANDROID!' | dd of="$image" bs=1 conv=notrunc status=none
word() {
    local file=$1 offset=$2 value=$3 encoded='' byte shift
    for shift in 0 8 16 24; do
        printf -v byte '\\%03o' "$(((value>>shift)&255))"
        encoded+="$byte"
    done
    printf '%b' "$encoded" | dd of="$file" bs=1 seek="$offset" conv=notrunc status=none
}
word "$image" 12 "$(stat -c %s "$work/original.lz4")"
word "$image" 40 4
dd if="$work/original.lz4" of="$image" bs=1M oflag=seek_bytes seek=4096 conv=notrunc status=none
mkdir "$work/extracted"
bwrap --ro-bind / / --bind "$work/extracted" /mnt --chdir /mnt \
    cpio -idm --quiet --no-absolute-filenames < "$work/original.cpio"
[[ $(stat -c %a "$staging/system/bin/demo") == 700 && $(stat -c %a "$work/extracted/system/bin/demo") == 755 ]]
bash "$component/scripts/check-packed-payload.sh" "$output" "$image" "$work/extracted" "$work/positive"
printf 'PASS exact replay accepts the actual fs_config mode conversion.\n'
cases=0
refuse() {
    local name=$1 selected_image=$2 selected_root=$3
    if bash "$component/scripts/check-packed-payload.sh" "$output" "$selected_image" "$selected_root" "$work/check-$name" \
        > "$work/$name.stdout" 2> "$work/$name.stderr"; then
        printf 'Unexpected packed-payload acceptance: %s\n' "$name" >&2; exit 1
    fi
    cases=$((cases+1)); printf 'REFUSED %s\n' "$name"
}
for name in content mode link extra missing; do cp -a -- "$work/extracted" "$work/$name"; done
printf altered > "$work/content/system/bin/demo"
refuse content "$image" "$work/content"
chmod 600 "$work/mode/system/bin/demo"
refuse mode "$image" "$work/mode"
unlink "$work/link/init"
ln -s /system/bin/foreign "$work/link/init"
refuse link "$image" "$work/link"
printf unexpected > "$work/extra/unselected"
refuse extra "$image" "$work/extra"
unlink "$work/missing/system/bin/demo"
refuse missing "$image" "$work/missing"
for name in .git .repo out out-clean out-public; do
    label="hidden-${name#.}"
    cp -a -- "$work/extracted" "$work/$label"
    mkdir "$work/$label/$name"
    printf 'Unexpected extracted member\n' > "$work/$label/$name/member"
    refuse "$label" "$image" "$work/$label"
done
printf changed > "$staging/system/bin/demo"
refuse changed-staging "$image" "$work/extracted"
printf 'Source-built packaging fixture\n' > "$staging/system/bin/demo"
for name in compressed kernel version length; do cp --sparse=always --reflink=auto -- "$image" "$work/$name.img"; done
printf '\000' | dd of="$work/compressed.img" bs=1 seek=4096 conv=notrunc status=none
refuse compressed "$work/compressed.img" "$work/extracted"
word "$work/kernel.img" 8 1
refuse kernel "$work/kernel.img" "$work/extracted"
word "$work/version.img" 40 3
refuse version "$work/version.img" "$work/extracted"
word "$work/length.img" 12 65000001
refuse length "$work/length.img" "$work/extracted"
ln -s "$work/extracted" "$work/linked-root"
refuse linked-root "$image" "$work/linked-root"

# Exercise the pinned parser's real binary rule format in a sibling partition.
mkdir -p "$output/target/product/uke/vendor/etc"
config="$output/target/product/uke/vendor/etc/fs_config_files"
rule=system/bin/demo
length=$((((16+${#rule}+1)+7)/8*8))
truncate -s "$length" "$config"
word "$config" 0 "$((length | (0640<<16)))"
printf '%s\0' "$rule" | dd of="$config" bs=1 seek=16 conv=notrunc status=none
cp -- "$config" "$work/golden-config"
refuse new-sibling-config "$image" "$work/extracted"
"$tools/mkbootfs" -d "$output/target/product/uke/system" "$staging" > "$work/configured.cpio"
"$tools/lz4" -l -12 --favor-decSpeed < "$work/configured.cpio" > "$work/configured.lz4"
cp --sparse=always --reflink=auto -- "$image" "$work/configured.img"
word "$work/configured.img" 12 "$(stat -c %s "$work/configured.lz4")"
dd if="$work/configured.lz4" of="$work/configured.img" bs=1M oflag=seek_bytes seek=4096 conv=notrunc status=none
mkdir "$work/configured-root"
bwrap --ro-bind / / --bind "$work/configured-root" /mnt --chdir /mnt \
    cpio -idm --quiet --no-absolute-filenames < "$work/configured.cpio"
[[ $(stat -c %a "$work/configured-root/system/bin/demo") == 640 ]]
bash "$component/scripts/check-packed-payload.sh" "$output" "$work/configured.img" "$work/configured-root" "$work/configured-check"
printf 'PASS real vendor filesystem rule changes the archived mode to 0640.\n'
word "$config" 0 "$((length | (0600<<16)))"
refuse changed-sibling-config "$work/configured.img" "$work/configured-root"
unlink "$config"
ln -s "$work/golden-config" "$config"
refuse linked-config "$work/configured.img" "$work/configured-root"
unlink "$config"
rmdir "$output/target/product/uke/vendor/etc"
mkdir "$work/external-etc"
cp -- "$work/golden-config" "$work/external-etc/fs_config_files"
ln -s "$work/external-etc" "$output/target/product/uke/vendor/etc"
refuse linked-config-parent "$work/configured.img" "$work/configured-root"
printf 'PASS actual default/sibling packaging rules and %s independent refusal controls. Fixtures remain private.\n' "$cases"
