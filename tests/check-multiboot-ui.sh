#!/usr/bin/env bash
# Check wizard controls against the actual XML engine's binding requirements.
set -euo pipefail
export LC_ALL=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
xml="$component/src/device/xiaomi/uke/multiboot.xml"
keyboard="$component/src/upstream/orangefox-android16/bootable/recovery/gui/theme/portrait_hdpi/pages/templates/keyboard.xml"
locale="$component/src/device/xiaomi/uke/localization/tr_TR-ure.xml"
base_locale="$component/src/upstream/orangefox-android16/bootable/recovery/gui/theme/common/languages/tr_TR.xml"
english_locale="$component/src/upstream/orangefox-android16/bootable/recovery/gui/theme/common/languages/en.xml"
xmllint --noout "$xml" "$keyboard" "$locale" "$english_locale"
# English is loaded before the selected language while pages are constructed.
# Inline defaults still log a missing-resource error unless the key is registered.
mapfile -t keys < <({ rg -o '\{@ure_mb_[a-z0-9_]+' "$xml" | sed 's/^{@//';
    rg -o '\{@ure_btrfs_[a-z0-9_]+' "$component/src/device/xiaomi/uke/maintainer.xml" | sed 's/^{@//';
    printf '%s\n' ure_mb_restore_phrase ure_mb_apply_phrase ure_theme_session_only; } | sort -u)
for key in "${keys[@]}"; do
    [[ $(xmllint --xpath "count(/language/resources/string[@name='$key'])" "$english_locale") == 1 ]]
    [[ $(xmllint --xpath "count(/resources/string[@name='$key'])" "$locale") == 1 ]]
done
# The packager appends this overlay to the upstream language, never replaces it.
mapfile -t collisions < <(comm -12 \
    <(sed -n 's/.*<string name="\([a-zA-Z0-9_]*\)".*/\1/p' "$locale" | sort) \
    <(sed -n 's/.*<string name="\([a-zA-Z0-9_]*\)".*/\1/p' "$base_locale" | sort))
[[ ${#collisions[@]} == 0 ]] || { printf 'Translation overlay collides with upstream keys: %s\n' "${collisions[*]}" >&2; exit 1; }
count() { xmllint --xpath "count($1)" "$xml"; }
[[ $(count '/recovery/pages/page') == 9 ]]
for page in welcome select sizes size order filesystems fs review confirm; do
    node="/recovery/pages/page[@name='ure_mb_$page']"
    [[ $(count "$node/action[touch/@key='back']") == 1 ]]
    [[ $(count "$node/action[touch/@key='home']/action[@function='home']") == 1 ]]
done
# GUIListBox expands names once unless requireReload is present.
[[ $(count '//listbox[listitem[contains(@name,"%ure_mb_")]][not(data/@requireReload)]') == 0 ]]
# bs_btn supplies a transparent hit region; text needs an explicit font.
[[ $(count '//button[text][not(font/@resource) or not(font/@color)]') == 0 ]]
[[ $(count '/recovery/pages/page[@name="ure_mb_review"]//text[text="%ure_status%"]') == 1 ]]
[[ $(count '//action[@function="set" and text()="ure_mb_review_back=ure_mb_welcome"]') == 1 ]]
[[ $(count '//action[@function="set" and text()="ure_mb_filesystems_back=ure_mb_order"]') == 1 ]]

# Collect characters that can actually be entered with stock keys and the palette.
declare -A available=()
definitions="$(xmllint --xpath '//template[@name="keyboardtemplate"]/keyboard/*[starts-with(name(),"layout")]/*[starts-with(name(),"row")]/@*' "$keyboard") $(xmllint --xpath '//page[@name="ure_mb_confirm"]/keyboard/layout1/row1/@*' "$xml")"
while [[ $definitions =~ ^[^\"]*\"([^\"]*)\"(.*)$ ]]; do
    key=${BASH_REMATCH[1]}; definitions=${BASH_REMATCH[2]}
    if [[ $key == *:* ]]; then key=${key#*:}; fi
    if [[ $key =~ ^c:([0-9]+)$ ]]; then
        available[${BASH_REMATCH[1]}]=1
    elif [[ ${#key} == 1 ]]; then
        printf -v point '%d' "'$key"
        available[$point]=1
    fi
done
for phrase in ure_mb_apply_phrase ure_mb_restore_phrase; do
    value=$(xmllint --xpath "string(/resources/string[@name='$phrase'])" "$locale")
    [[ -n $value ]]
    for point in $(printf '%s' "$value" | iconv -f UTF-8 -t UTF-32LE | od -An -v -tu4); do
        [[ ${available[$point]:-0} == 1 ]] || { printf 'Consent phrase %s cannot enter code point %s.\n' "$phrase" "$point" >&2; exit 1; }
    done
done
printf '%s\n' 'Multiboot UI: visible controls, fresh labels, Back/Home routes, review errors and both Turkish keyboard phrases passed.'
