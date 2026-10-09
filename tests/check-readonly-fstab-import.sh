#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Host-only exact-source fstab import controls; no tablet or key access.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-check}
[[ $mode == check || $mode == candidate ]]
source_path="$component/src/upstream/orangefox-android16/bootable/recovery"
android_tree="$component/src/upstream/orangefox-android16"
compiler="$android_tree/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
rg -q 'ClangDefaultVersion[[:space:]]*= "clang-r547379"' "$android_tree/build/soong/cc/config/global.go"
mkdir -p "$component/build"
scratch=$(mktemp -d "$component/build/readonly-fstab-import-XXXXXXXX")
git clone --shared --no-checkout "$source_path" "$scratch/source" >/dev/null 2>&1
git -C "$scratch/source" checkout --detach 3d733672081bca3af42475a286145f4a8cdce4e7 >/dev/null 2>&1
bash "$component/scripts/prepare-recovery-patches.sh" "$scratch/source"
bash "$component/scripts/prepare-recovery-patches.sh" "$scratch/source" check
if [[ $mode == check ]]; then
    cmp "$scratch/source/partitionmanager.cpp" "$source_path/partitionmanager.cpp"
    cmp "$component/src/device/xiaomi/uke/ure-readonly-fstab-import.hpp" \
        "$source_path/ure-readonly-fstab-import.hpp"
fi
awk '/auto merge_readonly = / {copy=1} copy {print} copy && /^[[:space:]]*};$/ {exit}' \
    "$scratch/source/partitionmanager.cpp" > "$scratch/readonly-fstab-merge.inc"
[[ -s $scratch/readonly-fstab-merge.inc ]]
! rg -q 'goto parse;|delete data;|delete meta;|Path_Exists\(additional_fstab\)' \
    "$scratch/source/partitionmanager.cpp"
rg -q 'if \(!additional_fstab_readonly_approved\)' "$scratch/source/partitionmanager.cpp"
rg -q 'same_block_device\(imported.data.block_device, data->Primary_Block_Device\)' \
    "$scratch/source/partitionmanager.cpp"
rg -q 'same_block_device\(imported.metadata.block_device, meta->Primary_Block_Device\)' \
    "$scratch/source/partitionmanager.cpp"
common=(-std=c++17 -Wall -Wextra -Werror -O1 \
    -I"$component/src/device/xiaomi/uke" -I"$scratch")
for flavor in normal sanitized; do
    flags=()
    if [[ $flavor == sanitized ]]; then
        # Match the main project sanitizer gate: this pinned Android toolchain
        # has no host C++ vptr runtime. Address, other UB and leak checks remain.
        flags=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g)
    fi
    timeout 45 "$compiler" "${common[@]}" "${flags[@]}" \
        "$component/tests/ure/readonly_fstab_import.cpp" -o "$scratch/import-$flavor"
    timeout 45 "$compiler" "${common[@]}" "${flags[@]}" \
        "$component/tests/ure/readonly_fstab_merge.cpp" -o "$scratch/merge-$flavor"
    mkdir "$scratch/files-$flavor"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        timeout 30 "$scratch/import-$flavor" "$scratch/files-$flavor"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        timeout 30 "$scratch/merge-$flavor"
done

mkdir "$scratch/mutant"
sed '/partition->Mount_Read_Only = true;/d' "$scratch/readonly-fstab-merge.inc" > \
    "$scratch/mutant/readonly-fstab-merge.inc"
timeout 45 "$compiler" -std=c++17 -Wall -Wextra -Werror -O1 \
    -I"$component/src/device/xiaomi/uke" -I"$scratch/mutant" \
    "$component/tests/ure/readonly_fstab_merge.cpp" -o "$scratch/merge-no-ro"
set +e
timeout 30 "$scratch/merge-no-ro" > "$scratch/removed-ro.log" 2>&1
mutant_status=$?
set -e
[[ $mutant_status == 1 ]]
rg -q '^FAIL: effective partition RO state lost$' "$scratch/removed-ro.log"
sha256sum "$compiler" "$component/src/device/xiaomi/uke/ure-readonly-fstab-import.hpp" \
    "$component/patches/0048-readonly-vendor-fstab-import.patch" \
    "$scratch/source/partitionmanager.cpp" "$scratch/readonly-fstab-merge.inc" > "$scratch/SHA256SUMS"
printf '%s\n' 'Read-only vendor fstab source/host/sanitizer controls and removed-RO mutant passed; no crypto enablement, Android link, keys, mapper, TEE or device acceptance.'
