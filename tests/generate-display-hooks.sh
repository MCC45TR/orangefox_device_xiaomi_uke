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
        'static float scale_theme_w=1,scale_theme_h=1;' \
        'static int tw_x_offset=0,tw_y_offset=0,tw_w_offset=0,tw_h_offset=0;'
    awk '/^void ure_gui_density\(/ { copying=1 } /^int GUIAction::uremanager\(/ { exit } copying { print }' \
        "$component/src/device/xiaomi/uke/ure-gui.cpp"
    awk '/^int PageSet::LoadVariables\(/ { copying=1 } /^int PageSet::LoadPages\(/ { exit } copying { print }' \
        "$recovery/gui/pages.cpp"
    awk '/^std::string LoadAttrString\(/ { copying=1 } /^float LoadAttrFloat\(/ { exit } copying { print }' \
        "$recovery/gui/pages.cpp"
    awk '/^int PageManager::RunReload\(/ { copying=1 } /^void PageManager::SetStartPage\(/ { exit } copying { print }' \
        "$recovery/gui/pages.cpp"
    awk '/^std::string gui_parse_text\(/ { copying=1 } /^std::string gui_lookup\(/ { exit } copying { print }' "$recovery/gui/gui.cpp"
    awk '/^extern "C" void set_scale_values\(/ { copying=1 } copying { print }' "$recovery/gui/gui.cpp"
    awk '/^int GUIListBox::NotifyVarChange\(/ { copying=1 } /^size_t GUIListBox::GetItemCount\(/ { exit } copying { print }' "$recovery/gui/listbox.cpp"
    printf '%s\n' 'void initialize_actual_list_item(xml_node<>* child,[[maybe_unused]] const std::string& mVariable,const std::string& currentValue,ListItem& item) {'
    awk '/item.variableValue = gui_parse_text\(child->value\(\)\)/ { copying=1 } copying { print } copying && /item.action = NULL/ { exit }' "$recovery/gui/listbox.cpp"
    printf '%s\n' '}'
} > "$output"
for symbol in ure_gui_density ure_gui_variable PageManager::RunReload PageManager::RequestUreReload scale_theme_min; do
    rg -q -F "$symbol" "$output" || { echo 'Required renderer function is missing' >&2; exit 1; }
done
