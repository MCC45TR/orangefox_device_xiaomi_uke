#!/usr/bin/env bash
# Compile the actual project graph widget with bounded host graphics stand-ins.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Generated C++ output is required}
{
    printf '%s\n' '#include "layout-hooks.h"'
    awk '/^class UrePartitionMap / { copying=1 } /^void ure_gui_density\(/ { exit } copying { print }' \
        "$component/src/device/xiaomi/uke/ure-gui.cpp"
} > "$output"
for symbol in 'class UrePartitionMap' ure_create_partition_map partition_layout_bar; do
    rg -q -F "$symbol" "$output" || { echo 'Required partition widget is missing' >&2; exit 1; }
done
