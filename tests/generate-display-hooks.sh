#!/usr/bin/env bash
# Compile exact project hooks and reviewed renderer functions with host stand-ins.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Generated C++ output is required}
recovery="$component/src/upstream/orangefox-android16/bootable/recovery"
{
    printf '%s\n' '#include "display-hooks.h"' '#define TWRES "/twres/"' \
        '#define LOGINFO(...) do {} while (false)' '#define LOGERR(...) do {} while (false)' \
        'static std::atomic<bool> ure_reload_theme{false};' \
        'static float scale_theme_w=1,scale_theme_h=1;'
    awk '/^void ure_gui_density\(/ { copying=1 } /^int GUIAction::uremanager\(/ { exit } copying { print }' \
        "$component/src/device/xiaomi/uke/ure-gui.cpp"
    awk '/^int PageManager::RunReload\(/ { copying=1 } /^void PageManager::SetStartPage\(/ { exit } copying { print }' \
        "$recovery/gui/pages.cpp"
    awk '/^std::string gui_parse_text\(/ { copying=1 } /^std::string gui_lookup\(/ { exit } copying { print }' "$recovery/gui/gui.cpp"
    awk '/^extern "C" void set_scale_values\(/ { copying=1 } copying { print }' "$recovery/gui/gui.cpp"
} > "$output"
for symbol in ure_gui_density ure_gui_variable PageManager::RunReload PageManager::RequestUreReload scale_theme_min; do
    rg -q -F "$symbol" "$output" || { echo 'Required renderer function is missing' >&2; exit 1; }
done
