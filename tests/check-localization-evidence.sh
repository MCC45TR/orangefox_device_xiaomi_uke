#!/usr/bin/env bash
# Isolated exact-source inventory controls. These do not execute draft tooling.
set -euo pipefail
umask 077
export LC_ALL=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "$component/tests/check-localization-evidence.sh" "$@"
fi
work=$(mktemp -d "$component/build/localization-evidence-fixture-XXXXXXXX")
fixture="$work/component"
tree="$fixture/src/upstream/orangefox-android16"
mkdir -p "$fixture/scripts" "$fixture/configs" "$fixture/build"
cp "$component/configs/localization-inputs.json" "$fixture/configs/"
for script in localization-evidence localization-evidence-lib index-build-tree; do
    cp "$component/scripts/$script.sh" "$fixture/scripts/"
    cmp "$component/scripts/$script.sh" "$fixture/scripts/$script.sh"
done
while IFS= read -r path; do
    [[ -e $fixture/$path ]] && continue
    if [[ -d $component/$path ]]; then
        mkdir -p "$fixture/$path"
        printf 'Isolated inventory fixture, not the actual upstream dependency.\n' > "$fixture/$path/inventory-fixture"
    else
        mkdir -p "$fixture/${path%/*}"
        printf 'Isolated source inventory fixture.\n' > "$fixture/$path"
    fi
done < <(jq -r '.required[]' "$fixture/configs/localization-inputs.json")
printf '<manifest>\n' > "$fixture/manifests/orangefox-android16-uke.lock.xml"
while IFS= read -r path; do
    git -C "$tree/$path" init -q
    git -C "$tree/$path" -c user.name=InventoryFixture -c user.email=fixture@users.noreply.github.com \
        commit -q --allow-empty -m 'Create isolated dependency fixture'
    if [[ $path == external/libxml2 ]]; then
        printf '<project name="%s" revision="%s"/>\n' "$path" "$(git -C "$tree/$path" rev-parse HEAD)"
    else
        printf '<project path="%s" revision="%s"/>\n' "$path" "$(git -C "$tree/$path" rev-parse HEAD)"
    fi >> "$fixture/manifests/orangefox-android16-uke.lock.xml"
done < <(jq -r '.source_projects[]' "$fixture/configs/localization-inputs.json")
printf '</manifest>\n' >> "$fixture/manifests/orangefox-android16-uke.lock.xml"
common="$tree/bootable/recovery/gui/theme/common/languages"
extra="$tree/bootable/recovery/gui/theme/extra-languages/languages"
mkdir -p "$common" "$extra"
while IFS= read -r language; do
    printf '<language><resources><string name="fixture">%s</string></resources></language>\n' "$language" > "$common/$language.xml"
done < <(jq -r '.language_codes[]' "$fixture/configs/localization-inputs.json")
for file in scripts/translate-locales.sh src/device/xiaomi/uke/ure-localization.hpp \
    src/device/xiaomi/uke/ure-locale-keys.hpp src/device/xiaomi/uke/font-fallback.hpp \
    src/device/xiaomi/uke/localization/fonts/ure-cjk.ttf src/localization/catalog.cpp \
    src/localization/font-instance.cpp \
    src/upstream/orangefox-android16/external/roboto-fonts/RobotoStatic-Regular.ttf \
    src/upstream/orangefox-android16/external/roboto-fonts/NOTICE \
    src/upstream/orangefox-android16/external/jsoncpp/LICENSE \
    src/upstream/orangefox-android16/external/libxml2/Copyright \
    src/upstream/orangefox-android16/external/freetype/LICENSE.TXT; do
    mkdir -p "$fixture/${file%/*}"
    printf 'Fixture content; never a generator executable or licensed font.\n' > "$fixture/$file"
done
driver="$fixture/scripts/localization-evidence.sh"
refuse() {
    if "$@" > "$work/refused.log" 2>&1; then echo 'Expected localization evidence refusal.' >&2; exit 1; fi
}
snapshot() { bash "$driver" source > "$1"; }
snapshot "$work/source-before.json"
snapshot "$work/source-repeat.json"
cmp "$work/source-before.json" "$work/source-repeat.json"
jq -e '.language_count==32 and .validation.generator_executed==false and .validation.glyph_coverage==false and
    .validation.translation_semantics==false and .validation.license_closure==false and .validation.physical_device==false' \
    "$work/source-before.json" >/dev/null
changed() {
    snapshot "$work/source-after.json"
    if cmp -s "$work/source-before.json" "$work/source-after.json"; then echo 'Mutation was not indexed.' >&2; exit 1; fi
}
for file in "$common/"*.xml; do
    cp "$file" "$work/original"
    printf '<!-- Changed language input -->\n' >> "$file"
    changed
    cp "$work/original" "$file"
done
for path in scripts/translate-locales.sh src/device/xiaomi/uke/ure-localization.hpp \
    src/device/xiaomi/uke/ure-locale-keys.hpp src/device/xiaomi/uke/font-fallback.hpp \
    src/device/xiaomi/uke/localization/fonts/ure-cjk.ttf src/localization/catalog.cpp \
    src/localization/font-instance.cpp \
    src/upstream/orangefox-android16/external/roboto-fonts/RobotoStatic-Regular.ttf \
    src/upstream/orangefox-android16/external/roboto-fonts/NOTICE \
    src/upstream/orangefox-android16/external/jsoncpp/LICENSE \
    src/upstream/orangefox-android16/external/libxml2/Copyright \
    src/upstream/orangefox-android16/external/freetype/LICENSE.TXT; do
    cp "$fixture/$path" "$work/original"
    printf 'Changed byte input\n' >> "$fixture/$path"
    changed
    cp "$work/original" "$fixture/$path"
done
target="$fixture/src/device/xiaomi/uke/font-fallback.hpp"
chmod 0644 "$target"; changed; chmod 0600 "$target"
mkdir "$fixture/src/device/xiaomi/uke/localization/new-directory"; changed
rmdir "$fixture/src/device/xiaomi/uke/localization/new-directory"
printf 'New generated input\n' > "$fixture/src/localization/new-generated.hpp"; changed
rm "$fixture/src/localization/new-generated.hpp"
mv "$target" "$work/original"; changed
ln -s "$work/original" "$target"; changed
printf 'Changed external regular-link target\n' >> "$work/original"; changed
rm "$target"; mv "$work/original" "$target"
snapshot "$work/source-link-after.json"
# Inventory annotations are stable across repeat captures; timestamp-only
# changes are handled by the separate full Android compile receipt.
snapshot "$work/source-link-repeat.json"
cmp "$work/source-link-after.json" "$work/source-link-repeat.json"
# Invalid resource closure and unsafe filesystem objects refuse outright.
mv "$common/en.xml" "$work/en.xml"; refuse snapshot "$work/invalid.json"
mv "$work/en.xml" "$common/en.xml"
cp "$common/en.xml" "$extra/en.xml"; refuse snapshot "$work/invalid.json"; rm "$extra/en.xml"
cp "$common/en.xml" "$common/unknown.xml"; refuse snapshot "$work/invalid.json"; rm "$common/unknown.xml"
mkfifo "$fixture/src/localization/fifo"; refuse snapshot "$work/invalid.json"; rm "$fixture/src/localization/fifo"
ln -s "$work" "$fixture/src/localization/foreign-directory"
refuse snapshot "$work/invalid.json"; rm "$fixture/src/localization/foreign-directory"
ln -s missing "$fixture/src/localization/unresolved"
refuse snapshot "$work/invalid.json"; rm "$fixture/src/localization/unresolved"
git -C "$tree/external/jsoncpp" -c user.name=InventoryFixture -c user.email=fixture@users.noreply.github.com \
    commit -q --allow-empty -m 'Change dependency pin without changing content'
refuse snapshot "$work/invalid.json"
# Actual UI inventory producer must bind every asset group, modes and membership.
payload="$work/payload"
mkdir -p "$payload/twres/"{languages,fonts,images,pages} "$payload/sbin" "$payload/system/etc/ure/licenses"
for path in twres/languages/en.xml twres/fonts/Roboto.ttf twres/fonts/LICENSE.txt twres/images/icon.png \
    twres/pages/main.xml twres/ui.xml sbin/maintainer.xml system/etc/ure/licenses/font.txt; do
    printf 'GUI resource fixture\n' > "$payload/$path"
done
bash "$driver" ui "$payload" > "$work/ui-before.json"
for path in twres/languages/en.xml twres/fonts/Roboto.ttf twres/fonts/LICENSE.txt twres/images/icon.png \
    twres/pages/main.xml twres/ui.xml sbin/maintainer.xml system/etc/ure/licenses/font.txt; do
    cp "$payload/$path" "$work/original"
    printf 'Changed GUI bytes\n' >> "$payload/$path"
    bash "$driver" ui "$payload" > "$work/ui-after.json"
    ! cmp -s "$work/ui-before.json" "$work/ui-after.json"
    cp "$work/original" "$payload/$path"
done
chmod 0644 "$payload/twres/fonts/Roboto.ttf"
bash "$driver" ui "$payload" > "$work/ui-after.json"
! cmp -s "$work/ui-before.json" "$work/ui-after.json"
chmod 0600 "$payload/twres/fonts/Roboto.ttf"
mkdir "$payload/twres/.git"
printf 'This resource path must not be pruned as VCS.\n' > "$payload/twres/.git/asset"
bash "$driver" ui "$payload" > "$work/ui-after.json"
! cmp -s "$work/ui-before.json" "$work/ui-after.json"
rm "$payload/twres/.git/asset"; rmdir "$payload/twres/.git"
mv "$payload/twres/fonts/Roboto.ttf" "$work/font"
ln -s "$work/font" "$payload/twres/fonts/Roboto.ttf"
refuse bash "$driver" ui "$payload"
rm "$payload/twres/fonts/Roboto.ttf"; mv "$work/font" "$payload/twres/fonts/Roboto.ttf"
mv "$payload/system/etc/ure/licenses" "$work/licenses"
ln -s "$work/licenses" "$payload/system/etc/ure/licenses"
refuse bash "$driver" ui "$payload"
rm "$payload/system/etc/ure/licenses"; mv "$work/licenses" "$payload/system/etc/ure/licenses"
# Shared production publication predicate: metadata exercises resource matching,
# never a positive VM/build/visual result. The fixture has no acceptance fields.
jq -n --arg source "$(sha256sum "$work/source-before.json" | cut -d' ' -f1)" \
    --arg ui "$(sha256sum "$work/ui-before.json" | cut -d' ' -f1)" \
    --arg overlay "$(printf 'Overlay fixture identity' | sha256sum | cut -d' ' -f1)" \
    '{localization_inputs_sha256:$source,shipping_ui_assets_sha256:$ui,runs:[
      {localization_inputs_sha256:$source,shipping_ui_assets_sha256:$ui,overlay_ui_assets_sha256:$overlay,reviewed_ui_assets_sha256:$overlay},
      {localization_inputs_sha256:$source,shipping_ui_assets_sha256:$ui,overlay_ui_assets_sha256:$overlay,reviewed_ui_assets_sha256:$overlay}]}' \
    > "$work/matching-resources.json"
bash "$driver" verify-gui "$work/source-before.json" "$work/ui-before.json" "$work/matching-resources.json"
for change in 'del(.shipping_ui_assets_sha256)' '.localization_inputs_sha256="old-source"' \
    '.runs[0].shipping_ui_assets_sha256="old-font"' '.runs[1].overlay_ui_assets_sha256=""' \
    '.runs[0].reviewed_ui_assets_sha256="different-overlay"' '.runs=[]'; do
    jq "$change" "$work/matching-resources.json" > "$work/invalid-resources.json"
    refuse bash "$driver" verify-gui "$work/source-before.json" "$work/ui-before.json" "$work/invalid-resources.json"
done
refuse bash "$driver" verify-gui "$work/source-after.json" "$work/ui-before.json" "$work/matching-resources.json"
refuse bash "$driver" verify-gui "$work/source-before.json" "$work/ui-after.json" "$work/matching-resources.json"
printf '%s\n' 'All 32 language mutations, draft headers/generators/fonts/licenses, mode/link/membership/pin controls and old GUI resource receipt refusals passed; no generator, font renderer, guest or tablet acceptance is asserted.'
