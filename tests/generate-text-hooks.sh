#!/usr/bin/env bash
# Extract actual rotation, scale-wrapper and console-wrap production paths.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
recovery="$component/src/upstream/orangefox-android16/bootable/recovery"
output=${1:?Generated C++ output is required}
{
    printf '%s\n' '#include "text-renderer.h"'
    awk '/^int ROTATION_X_DISP\(/ { copying=1 } /^void gr_draw_rect\(/ { exit } copying { print }' "$recovery/minuitwrp/graphics_utils.cpp"
    awk '/^int gr_textEx_scaleW\(/ { copying=1 } /^\/\/ Active clip-region/ { exit } copying { print }' "$recovery/minuitwrp/graphics.cpp"
    awk '/^bool GUIScrollList::AddLines\(/ { copying=1 } /^(bool|int|void) GUIScrollList::/ && copying && !/^bool GUIScrollList::AddLines/ { exit } copying { print }' "$recovery/gui/scrolllist.cpp"
} > "$output"
for symbol in surface_ROTATION_transform ScaledFontLease GUIScrollList::AddLines; do rg -q -F "$symbol" "$output"; done
