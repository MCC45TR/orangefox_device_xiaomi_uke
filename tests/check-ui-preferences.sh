#!/usr/bin/env bash
# Host-only worker controls; no device mount or calibration access.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
work=$(mktemp -d "$component/build/preference-worker-check-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
for flavor in native sanitized; do
    flags=()
    [[ $flavor != sanitized ]] || flags=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g)
    "$compiler" -std=c++20 -Wall -Wextra -Werror -UNDEBUG -O1 -pthread "${flags[@]}" \
        -I "$component/src/device/xiaomi/uke/recoveryctl/libuke" \
        "$component/tests/ure/ui_preference_writer.cpp" -o "$work/$flavor"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 15 "$work/$flavor"
done
printf '%s\n' 'Shipping preference worker: coalescing, responsive requests during blocked saves, final drain and failure cleanup passed; device persistence remains separate.'
