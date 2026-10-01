#!/usr/bin/env bash
# Host-only exact reviewed keyboard implementation with GUI boundary stand-ins.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Generated C++ output is required}
source="$component/src/upstream/orangefox-android16/bootable/recovery/gui/hardwarekeyboard.cpp"
{
    printf '%s\n' '#include "input-hooks.h"'
    awk '/^HardwareKeyboard::HardwareKeyboard\(/ { copying=1 } copying { print }' "$source"
} > "$output"
for symbol in ResetPressedKeys KEY_F6 MoveFocus SelectFocusedElement; do rg -q -F "$symbol" "$output"; done
