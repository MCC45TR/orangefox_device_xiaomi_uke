#!/usr/bin/env bash
# Compile the actual input encoder, cursor and editing handlers on the host.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "$component/build/localized-input-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
source=${UKE_INPUT_SOURCE:-$component/src/upstream/orangefox-android16/bootable/recovery/gui/input.cpp}
awk '/^static std::string InputCharacter\(/ {copy=1} /^int GUIInput::NotifyCharInput\(/ {exit} copy {print}' \
    "$source" > "$work/input.inc"
rg -q '^static std::size_t PreviousInputCharacter' "$work/input.inc"
rg -q '^static std::size_t NextInputCharacter' "$work/input.inc"
awk '/^void GUIInput::HandleCursorByTouch\(/ {copy=1} /^void GUIInput::HandleCursorByText\(/ {exit} copy {print}' "$source" > "$work/input-touch.inc"
awk '/^int GUIInput::NotifyKey\(/ {copy=1} /^static std::string InputCharacter\(/ {exit} copy {print}' "$source" > "$work/input-key.inc"
awk '/^int GUIInput::NotifyCharInput\(/ {copy=1} copy {print; if ($0=="}") exit}' "$source" > "$work/input-edit.inc"
c++ -std=c++17 -Wall -Wextra -Werror -I"$work" "$component/tests/ure/localized_input.cpp" -o "$work/test"
"$work/test"
