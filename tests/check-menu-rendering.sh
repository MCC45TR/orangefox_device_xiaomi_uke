#!/usr/bin/env bash
# Execute the shipping row-rendering function with host graphics stand-ins.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "$component/build/menu-rendering-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
{
    printf '%s\n' '#include "menu-rendering.h"'
    awk '/^void GUIScrollList::RenderStdItem\(/ {copying=1} /^int GUIScrollList::Update\(/ {exit} copying {print}' \
        "$component/src/upstream/orangefox-android16/bootable/recovery/gui/scrolllist.cpp"
} > "$work/renderer.cpp"
c++ -std=c++20 -O2 -Wall -Wextra -Werror -Wno-class-memaccess -I "$component/tests/ure" \
    "$work/renderer.cpp" "$component/tests/ure/menu-rendering.cpp" -o "$work/check"
"$work/check"
xml="$component/src/device/xiaomi/uke/maintainer.xml"
xmllint --noout "$xml"
[[ $(xmllint --xpath 'count(//listitem)' "$xml") == "$(xmllint --xpath 'count(//listitem[@description])' "$xml")" ]]
[[ $(xmllint --xpath 'count(//page[@name="ure_display"]//listbox[data[@name="ure_scale_choice"]]/listitem/action)' "$xml") == 0 ]]
[[ $(xmllint --xpath 'count(//page[@name="ure_display"]//button/action[@function="uremanager" and text()="scale-apply"])' "$xml") == 1 ]]
[[ $(xmllint --xpath 'count(//page[@name="ure_display_options"]//button/action[@function="uremanager" and text()="scale-apply"])' "$xml") == 1 ]]
[[ $(xmllint --xpath 'count(//page[@name="ure_display_storage"]//action[@function="uremanager" and text()="scale-apply"])' "$xml") == 0 ]]
[[ $(xmllint --xpath 'count(//page[@name="ure_display"]//listbox[data[@name="ure_scale_choice"]]/listitem)' "$xml") == 5 ]]
[[ $(xmllint --xpath 'count(//page[@name="ure_display_options"]//listbox[data[@name="ure_scale_choice"]]/listitem)' "$xml") == 11 ]]
for percent in 50 55 60 65 70 75 80 85 90 95 100; do
    [[ $(xmllint --xpath "count(//page[@name='ure_display_options']//listitem[text()='$percent' and not(action)])" "$xml") == 1 ]]
    [[ $(xmllint --xpath "count(//page[@name='ure_monitor_scale']//listitem[text()='$percent' and not(action)])" "$xml") == 1 ]]
done
[[ $(xmllint --xpath 'count(//page[@name="ure_display" or @name="ure_display_options"]/urescalepreview)' "$xml") == 2 ]]
# Stock pages instantiate their footer before the late maintainer include.
# Their images must already exist in the shared resource set at that point.
images="$component/src/upstream/orangefox-android16/bootable/recovery/gui/theme/portrait_hdpi/resources/images.xml"
for name in ure_extra_icon ure_extra_active; do
    [[ $(xmllint --xpath "count(//resources/image[@name='$name' and @filename='URE/layout-grid'])" "$images") == 1 ]]
    [[ $(xmllint --xpath "count(//resources/image[@name='$name'])" "$xml") == 0 ]]
done
origins="$component/src/device/xiaomi/uke/ui-icons/ORIGINS.json"
while read -r name sha; do
    [[ $(sha256sum "$component/src/device/xiaomi/uke/ui-icons/$name.png" | cut -d' ' -f1) == "$sha" ]]
done < <(jq -r '.icons[]|"\(.name) \(.png_sha256)"' "$origins")
echo 'Theme entries contain descriptions; scale presets and custom input require explicit Apply; pinned icon checksums match.'
