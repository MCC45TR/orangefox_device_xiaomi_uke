#!/usr/bin/env bash
# Compile the production decoder without copying its implementation.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Generated C++ output is required}
{
    printf '%s\n' '#include "text-decoder.h"'
    awk '/^int twrpTruetype::utf8_to_unicode\(/ { copying=1 } /^void\* twrpTruetype::gr_ttf_loadFont\(/ { exit } copying { print }' \
        "$component/src/upstream/orangefox-android16/bootable/recovery/minuitwrp/truetype.cpp"
} > "$output"
rg -q -F 'const char* end' "$output"
