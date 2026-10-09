#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Actual pinned function controls, with inert host boundaries and no device I/O.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-check}
[[ $mode == check || $mode == candidate ]]
android_tree="$component/src/upstream/orangefox-android16"
source_path="$android_tree/system/vold"
compiler="$android_tree/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
rg -q 'ClangDefaultVersion[[:space:]]*= "clang-r547379"' "$android_tree/build/soong/cc/config/global.go"
mkdir -p "$component/build"
scratch=$(mktemp -d "$component/build/vold-key-upgrade-XXXXXXXX")
git clone --shared --no-checkout "$source_path" "$scratch/source" >/dev/null 2>&1
git -C "$scratch/source" checkout --detach 953de9608eb78380b3c4e39e801c2bc0af7dbddc >/dev/null 2>&1
bash "$component/scripts/prepare-reviewed-patches.sh" "$scratch/source" apply vold
bash "$component/scripts/prepare-reviewed-patches.sh" "$scratch/source" check vold
if [[ $mode == check ]]; then cmp "$scratch/source/KeyStorage.cpp" "$source_path/KeyStorage.cpp"; fi
extract_function() {
    awk '/^static KeystoreOperation BeginKeystoreOp/ {copy=1} \
        copy && /^static bool encryptWithKeystoreKey/ {exit} copy {print}' "$1" > "$2"
    [[ -s $2 ]]
}
extract_function "$scratch/source/KeyStorage.cpp" "$scratch/vold-begin-keystore-op.inc"
common=(-std=c++17 -Wall -Wextra -Werror -D_GLIBCXX_ASSERTIONS -pthread -O1 \
    -Wl,--wrap=mkdir -I"$scratch")
for flavor in normal sanitized; do
    flags=()
    if [[ $flavor == sanitized ]]; then
        # The pinned host runtime excludes C++ vptr instrumentation, as does
        # the aggregate sanitizer gate. Address, other UB and leaks stay on.
        flags=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g)
    fi
    timeout 45 "$compiler" "${common[@]}" "${flags[@]}" \
        "$component/tests/ure/vold_key_upgrade.cpp" -o "$scratch/test-$flavor"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        timeout 30 "$scratch/test-$flavor"
done

# Real std::optional with the host library's checked dereference makes the
# proven original nullopt defect deterministic, without touching real keys.
mkdir "$scratch/original" "$scratch/removed-guard"
git -C "$source_path" show HEAD:KeyStorage.cpp > "$scratch/original.cpp"
extract_function "$scratch/original.cpp" "$scratch/original/vold-begin-keystore-op.inc"
sed '/if (!upgraded_blob) return opHandle;/d' "$scratch/vold-begin-keystore-op.inc" > \
    "$scratch/removed-guard/vold-begin-keystore-op.inc"
ulimit -c 0
for mutant in original removed-guard; do
    timeout 45 "$compiler" -std=c++17 -Wall -Wextra -Werror -D_GLIBCXX_ASSERTIONS \
        -pthread -O1 -Wl,--wrap=mkdir -I"$scratch/$mutant" \
        "$component/tests/ure/vold_key_upgrade.cpp" -o "$scratch/test-$mutant"
    set +e
    bash -c 'timeout 30 "$1" no-upgrade; exit "$?"' _ "$scratch/test-$mutant" \
        > "$scratch/$mutant.log" 2>&1
    mutant_status=$?
    set -e
    [[ $mutant_status == 134 ]]
    rg -q '_M_is_engaged' "$scratch/$mutant.log"
done
sha256sum "$compiler" "$component/patches/0050-vold-optional-key-upgrade.patch" \
    "$scratch/source/KeyStorage.cpp" "$scratch/vold-begin-keystore-op.inc" > "$scratch/SHA256SUMS"
printf '%s\n' 'Pinned vold BeginKeystoreOp normal/sanitizer controls pass; original and removed-guard nullopt defects rejected. All key I/O intercepted; crypto remains disabled.'
