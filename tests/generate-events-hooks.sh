#!/usr/bin/env bash
# Compile exact event discovery/hotplug code with a fake evdev OS boundary.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Generated output is required}
source="$component/src/upstream/orangefox-android16/bootable/recovery/minuitwrp/events.cpp"
{
    printf '%s\n' '#include "events-hooks.h"'
    awk '/^#define MAX_DEVICES/ { copying=1 } copying { print }' "$source"
} > "$output"
for symbol in SYN_DROPPED POLLHUP st_mtim O_NONBLOCK; do rg -q -F "$symbol" "$output"; done
