#!/usr/bin/env bash
# Compile the actual preview widget with host graphics and font stand-ins.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Generated C++ output is required}
{
    printf '%s\n' '#include "scale-preview.h"'
    awk '/^class UreScalePreview / {copying=1} /^class UrePartitionMap / {exit} copying {print}' \
        "$component/src/device/xiaomi/uke/ure-gui.cpp"
} > "$output"
rg -q 'ure_create_scale_preview' "$output"
