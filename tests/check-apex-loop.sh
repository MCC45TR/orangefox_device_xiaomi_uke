#!/usr/bin/env bash
# Host-only controls for the source-extracted APEX loader; no block-device access.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_dir="$component/src/upstream/orangefox-android16/bootable/recovery"
bash "$component/scripts/prepare-reviewed-patches.sh" "$source_dir" check recovery
work=$(mktemp -d "$component/build/apex-loop-test-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
awk '/^bool twrpApex::mountApexOnLoopbackDevices\(/ {copy=1}
     /^bool twrpApex::Unmount\(/ {exit} copy {print}' "$source_dir/twrpApex.cpp" > "$work/apex-loop.inc"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-unused-function -UNDEBUG -O1 -I "$work")
for flavor in native sanitized; do
    instrumentation=()
    if [[ $flavor == sanitized ]]; then instrumentation=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g); fi
    "$compiler" "${flags[@]}" "${instrumentation[@]}" "$component/tests/ure/apex_loop.cpp" -o "$work/$flavor"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$work/$flavor"
done
cp "$work/apex-loop.inc" "$work/accepted.inc"
ulimit -c 0
for mutant in closed-size wrong-loop; do
    case $mutant in
        closed-size) sed 's/info.lo_sizelimit = payload.st_size;/info.lo_sizelimit = lseek(fd, 0, SEEK_END);/' "$work/accepted.inc" ;;
        wrong-loop) sed 's/loadApexImage(fileToMount, num)/loadApexImage(fileToMount, 0)/' "$work/accepted.inc" ;;
    esac > "$work/apex-loop.inc"
    "$compiler" "${flags[@]}" "$component/tests/ure/apex_loop.cpp" -o "$work/mutant"
    set +e
    bash -c '"$1"; exit "$?"' _ "$work/mutant" > "$work/$mutant.log" 2>&1
    status=$?
    set -e
    [[ $status == 134 ]]
done
printf '%s\n' 'Both defect-restoring mutants refused. Tablet APEX loading and decryption remain separate acceptance gates.'
