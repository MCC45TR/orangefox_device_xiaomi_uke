#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_tree="$component/src/upstream/orangefox-android16/bootable/recovery"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
work=$(mktemp -d "$component/build/native-theme-check-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
awk '/^int GUIAction::uretheme\(/ {copy=1} copy {print} copy && /^}/ {exit}' "$source_tree/gui/action.cpp" > "$work/theme-handler.inc"
rg -q 'ure::theme::prepare' "$work/theme-handler.inc"
if rg -n 'Exec_Cmd|Mount_By_Path|SaveValues|ReadSettingsFile' "$work/theme-handler.inc"; then exit 1; fi
[[ $(rg -c 'DataManager::QueuePreferences\(\)' "$work/theme-handler.inc") == 1 ]]
rg -q 'ure_theme_active' "$work/theme-handler.inc"
for flavor in native sanitized; do
    flags=()
    [[ $flavor != sanitized ]] || flags=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g)
    timeout 45 "$compiler" -std=c++20 -Wall -Wextra -Werror -UNDEBUG -O1 -pthread "${flags[@]}" \
        -I "$work" -I "$component/src/device/xiaomi/uke" -I "$source_tree/gui" \
        "$component/tests/ure/theme.cpp" -o "$work/$flavor"
    mkdir "$work/$flavor-fixture"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 "$work/$flavor" \
        "$source_tree/gui/theme/portrait_hdpi" "$work/$flavor-fixture"
done
xmllint --noout "$source_tree/gui/theme/portrait_hdpi/pages/customization.xml" "$source_tree/gui/theme/portrait_hdpi/pages/templates/templates.xml"
printf '%s\n' 'Native packaged theme and preference handoff tests passed; device persistence and physical rendering remain separate.'
