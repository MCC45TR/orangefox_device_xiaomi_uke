#!/usr/bin/env bash
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source=${UKE_KEYBOARD_SOURCE:-$component/src/upstream/orangefox-android16/bootable/recovery/gui/keyboard.cpp}
work=$(mktemp -d "$component/build/keyboard-geometry-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
awk '/^void GUIKeyboard::FitToWidth\(/ {copying=1} /^int GUIKeyboard::SetRenderPos\(/ {exit} copying {print}' "$source" > "$work/keyboard-fit.inc"
awk '/^GUIKeyboard::Key\* GUIKeyboard::HitTestKey\(/ {copying=1} /^int GUIKeyboard::NotifyTouch\(/ {exit} copying {print}' "$source" > "$work/keyboard-hit.inc"
rg -q '^void GUIKeyboard::FitToWidth' "$work/keyboard-fit.inc"
compiler=${CXX:-c++}; flags=()
if [[ ${UKE_KEYBOARD_SANITIZER:-0} == 1 ]]; then
    compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
    flags=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"$compiler" -std=c++20 -Wall -Wextra -Werror "${flags[@]}" -I"$work" "$component/tests/ure/keyboard_geometry.cpp" -o "$work/test"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$work/test"
