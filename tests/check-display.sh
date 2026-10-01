#!/usr/bin/env bash
# Isolated CLI fixtures; never mount a filesystem or change host display settings.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-$component/build/ure-host/uke-recoveryctl}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"$binary" display preview 2136 3200 1080 3200 75 > "$work/layout"
jq -e '.data.scale_percent==75 and .data.density==0.75 and .data.canvas_width==2848 and .data.canvas_height==4267 and
    .data.uniform_density and (.data.physical_test_record==false) and (.data.gui_rendering==false)' "$work/layout" >/dev/null
for percent in 50 60 70 75 80 90 100; do
    "$binary" display preview 3200 2136 1080 3200 "$percent" > "$work/rotated"
    jq -e --argjson percent "$percent" '.data.scale_percent==$percent and .data.canvas_width>=3200 and .data.canvas_height>=2136' "$work/rotated" >/dev/null
done
"$binary" display settings-save "$work/settings" 60 > "$work/save"
jq -e '.data.scale_percent==60 and .data.private_record and (.data.mounts_performed==false)' "$work/save" >/dev/null
[[ $(stat -c %a "$work/settings") == 700 && $(stat -c %a "$work/settings/display.json") == 600 ]]
"$binary" display settings-load "$work/settings" > "$work/read"
jq -e '.data.scale_percent==60' "$work/read" >/dev/null
"$binary" display settings-save "$work/settings" 85 > "$work/save"
"$binary" display settings-load "$work/settings" > "$work/read"
jq -e '.data.scale_percent==85' "$work/read" >/dev/null
before=$(sha256sum "$work/settings/display.json" | cut -d' ' -f1)
for percent in 49 101 75.5 invalid; do
    if "$binary" display settings-save "$work/settings" "$percent" > "$work/error"; then echo 'Invalid scale was accepted' >&2; exit 1; fi
    jq -e '.error.code=="invalid-scale"' "$work/error" >/dev/null
done
[[ $before == "$(sha256sum "$work/settings/display.json" | cut -d' ' -f1)" ]]
if "$binary" display settings-load "$work/settings" --root "$work" > "$work/error"; then echo 'Unrelated root option was accepted' >&2; exit 1; fi
jq -e '.error.code=="invalid-options"' "$work/error" >/dev/null
chmod 644 "$work/settings/display.json"
if "$binary" display settings-load "$work/settings" > "$work/error"; then echo 'Public settings were accepted' >&2; exit 1; fi
jq -e '.error.code=="unsafe-settings"' "$work/error" >/dev/null
printf 'Display CLI: tablet/rotated presets, private durable settings/readback, invalid input and context refusals passed; no display or tablet was changed.\n'
