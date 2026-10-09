#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Synthetic policy controls. Never run the privileged helper on the host.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
work=$(mktemp -d "$component/build/clock-mount-check-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
source_dir="$component/src/device/xiaomi/uke/clock-sync"
for flavor in native sanitized; do
    flags=()
    [[ $flavor != sanitized ]] || flags=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g)
    timeout 45 "$compiler" -std=c++20 -O1 -Wall -Wextra -Werror "${flags[@]}" \
        -I "$source_dir" "$component/tests/ure/clock_mount.cpp" -o "$work/$flavor"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 15 "$work/$flavor"
done
timeout 45 "$compiler" -std=c++20 -O1 -Wall -Wextra -Werror \
    -I "$component/src/device/xiaomi/uke" "$source_dir/clock-sync.cpp" -o "$work/production-host"
# A root caller must not execute this host binary, even to test refusal.
if (( UID != 0 )); then
    status=0
    "$work/production-host" || status=$?
    [[ $status == 21 ]]
fi
rg -q 'static_executable: true' "$source_dir/Android.bp"
rg -q '^    uke-clock-sync ' "$component/src/device/xiaomi/uke/device.mk"
rg -q '^service uke-clock-sync /system/bin/uke-clock-sync$' "$component/src/device/xiaomi/uke/recovery/root/init.recovery.qcom.rc"
printf '%s\n' 'Private clock-mount policies and production compilation passed; host time was not changed.'
