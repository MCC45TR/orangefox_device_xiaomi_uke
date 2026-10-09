#!/usr/bin/env bash
# Host-only subprocess/ownership controls; never loads an OEM HAL or a module.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "$component/build/touch-session-XXXXXXXX")
source_dir="$component/src/device/xiaomi/uke/touch"
mkdir -p "$work/host-check"
compiler=${CXX:-g++}
"$compiler" -std=c++20 -O0 -Wall -Wextra -Werror -I "$source_dir" "$component/tests/touch-session/host-child.cpp" -o "$work/host-check/fixture"
for mode in hang stubborn exit flood manager; do
    ln -f "$work/host-check/fixture" "$work/host-check/fixture-$mode"
done
cd "$work"
for mode in production-path owned-gui; do
    flags=()
    [[ $mode != owned-gui ]] || flags+=(-DUKE_TOUCH_GUI_HOST_CONTROLS)
    "$compiler" -std=c++20 -O0 -Wall -Wextra -Werror -I "$source_dir" "${flags[@]}" \
        "$component/tests/touch-session/host-controls.cpp" "$source_dir/touch-gui-session.cpp" \
        -lcrypto -pthread -o "$work/controls-$mode"
    timeout 45 "$work/controls-$mode" "$work/host-check"
done
clang="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$clang" | cut -d ' ' -f 1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
"$clang" -std=c++20 -O0 -g -Wall -Wextra -Werror -I "$source_dir" \
    -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer \
    -DUKE_TOUCH_GUI_HOST_CONTROLS "$component/tests/touch-session/host-controls.cpp" \
    "$source_dir/touch-gui-session.cpp" -lcrypto -pthread -o "$work/controls-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    timeout 45 "$work/controls-sanitized" "$work/host-check"
printf 'Touch session host and sanitizer controls passed; no OEM or device operation.\n'
