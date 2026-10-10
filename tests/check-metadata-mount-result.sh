#!/usr/bin/env bash
# Source-extracted host control; no filesystem mounts or Android services.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_dir="$component/src/upstream/orangefox-android16/system/vold"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(git -C "$source_dir" rev-parse HEAD) == 953de9608eb78380b3c4e39e801c2bc0af7dbddc ]]
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
bash "$component/scripts/prepare-reviewed-patches.sh" "$source_dir" check vold
work=$(mktemp -d "$component/build/metadata-mount-result-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
awk '/LOG\(INFO\) << "Mounting metadata-encrypted filesystem:"/ {copy=1}
     copy {print} copy && /^}/ {exit}' "$source_dir/MetadataCrypt.cpp" > "$work/metadata-mount-completion.inc"
flags=(-std=c++20 -Wall -Wextra -Werror -UNDEBUG -O1 -I "$work")
for flavor in native sanitized; do
    instrumentation=()
    if [[ $flavor == sanitized ]]; then instrumentation=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g); fi
    "$compiler" "${flags[@]}" "${instrumentation[@]}" "$component/tests/ure/metadata_mount_result.cpp" -o "$work/test-$flavor"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$work/test-$flavor"
done
cp -- "$work/metadata-mount-completion.inc" "$work/accepted.inc"
sed '/if (!mount_via_fs_mgr/,/^    }/c\    mount_via_fs_mgr(mount_point.c_str(), crypto_blkdev.c_str(), needs_encrypt);' \
    "$work/accepted.inc" > "$work/metadata-mount-completion.inc"
"$compiler" "${flags[@]}" "$component/tests/ure/metadata_mount_result.cpp" -o "$work/mutant"
ulimit -c 0
set +e
bash -c '"$1"; exit "$?"' _ "$work/mutant" > "$work/mutant.log" 2>&1
status=$?
set -e
[[ $status == 134 ]]
printf '%s\n' 'Removed mount-result guard rejected; metadata decryption is not physically accepted by this host control.'
