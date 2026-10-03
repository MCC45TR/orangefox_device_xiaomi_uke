#!/usr/bin/env bash
# Compile actual management callbacks with host UI variables, without an Android window.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Generated C++ output is required}
{
    printf '%s\n' '#include "management-hooks.h"'
    awk '/^namespace \{/ { copying=1 } /^class UreScalePreview / { exit } copying { print }' "$component/src/device/xiaomi/uke/ure-gui.cpp"
    awk '/^int GUIAction::uremanager\(/ { copying=1 } copying { print }' "$component/src/device/xiaomi/uke/ure-gui.cpp"
} > "$output"
for symbol in GUIAction::uremanager filesystem_operation_execute linux_rescue_execute btrfs_manage_execute; do
    rg -q -F "$symbol" "$output" || { echo 'Actual management callback is missing' >&2; exit 1; }
done
