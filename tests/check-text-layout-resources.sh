#!/usr/bin/env bash
# Exact production packagers in isolated namespaces; no font/code generators.
set -euo pipefail
umask 077
export LC_ALL=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "$component/tests/check-text-layout-resources.sh" "$@"
fi
work=$(mktemp -d "$component/build/text-resource-fixture-XXXXXXXX")
fixture="$work/component"
tree="$fixture/src/upstream/orangefox-android16"
device="$fixture/src/device/xiaomi/uke"
mkdir -p "$fixture/"{scripts,manifests,configs/text-layout,patches,build,referances/upstream/fribidi-release} \
    "$device/"{localization,text-notices} "$tree/external"
for file in prepare-font-resources.sh prepare-text-notices.sh; do cp "$component/src/device/xiaomi/uke/$file" "$device/"; done
cp "$component/src/device/xiaomi/uke/text-notices/Unicode-LICENSE.txt" "$device/text-notices/"
cp "$component/manifests/text-layout.lock.json" "$fixture/manifests/"
cp "$component/configs/text-layout/"* "$fixture/configs/text-layout/"
cp "$component/patches/0031-fribidi-allocation-failure.patch" "$fixture/patches/"
cp "$component/patches/0033-fribidi-explicit-direction-types.patch" "$fixture/patches/"
for script in check-font-resources prepare-text-layout-sources prepare-android-text-layout; do
    cp "$component/scripts/$script.sh" "$fixture/scripts/"
done
refuse() {
    local status
    if timeout 30 "$@" > "$work/refused.log" 2>&1; then echo 'Expected text-resource refusal.' >&2; exit 1;
    else status=$?; fi
    [[ $status -lt 124 ]] || { echo 'A timeout or signal is not a validated refusal.' >&2; exit 1; }
    [[ $(tail -c 10000 "$work/refused.log" | wc -c) -le 10000 ]]
}
snapshot() {
    find "$1" -type f -exec sha256sum {} + | sort | sha256sum | cut -d' ' -f1
}
# Synthetic regular assets exercise publication guards, never glyph/license
# acceptance. Their paths and the production helper are unchanged.
: > "$device/localization/fonts.lock.tsv"
index=0
while IFS=$'\t' read -r asset notice; do
    mkdir -p "$tree/external/noto-fonts/${asset%/*}"
    printf 'Isolated font bytes %s; not a real font.\n' "$index" > "$tree/external/noto-fonts/$asset"
    printf 'Isolated license bytes %s; not a license grant.\n' "$index" > "$tree/external/noto-fonts/$notice"
    digest=$(sha256sum "$tree/external/noto-fonts/$asset" | cut -d' ' -f1)
    notice_digest=$(sha256sum "$tree/external/noto-fonts/$notice" | cut -d' ' -f1)
    printf '%s\t%s\t%s\t%s\n' "$digest" "$asset" "$notice" "$notice_digest" >> "$device/localization/fonts.lock.tsv"
    jq --argjson index "$index" --arg digest "$digest" --arg notice "$notice_digest" \
        '.fonts.files[$index].sha256=$digest | .fonts.files[$index].notice_sha256=$notice' \
        "$fixture/manifests/text-layout.lock.json" > "$work/lock.json"
    mv "$work/lock.json" "$fixture/manifests/text-layout.lock.json"
    ((index+=1))
done < <(jq -r '.fonts.files[]|[.path,.notice]|@tsv' "$component/manifests/text-layout.lock.json")
payload="$work/payload"
packager="$device/prepare-font-resources.sh"
checker="$fixture/scripts/check-font-resources.sh"
bash "$packager" "$tree" "$payload"
bash "$checker" "$payload"
before=$(snapshot "$payload")
bash "$packager" "$tree" "$payload"
[[ $(snapshot "$payload") == "$before" ]]
for kind in path notice; do
    while IFS= read -r asset; do
        source="$tree/external/noto-fonts/$asset"
        cp "$source" "$work/original"
        printf 'Corruption\n' >> "$source"
        refuse bash "$packager" "$tree" "$payload"
        [[ $(snapshot "$payload") == "$before" ]]
        cp "$work/original" "$source"
        mv "$source" "$work/removed"
        refuse bash "$packager" "$tree" "$payload"
        ln -s "$work/removed" "$source"
        refuse bash "$packager" "$tree" "$payload"
        rm "$source"; mv "$work/removed" "$source"
    done < <(jq -r --arg kind "$kind" '.fonts.files[]|.[$kind]' "$fixture/manifests/text-layout.lock.json")
done
source_parent="$tree/external/noto-fonts/notosanshebrew"
mv "$source_parent" "$work/source-parent"
ln -s "$work/source-parent" "$source_parent"
refuse bash "$packager" "$tree" "$payload"
[[ $(snapshot "$payload") == "$before" ]]
rm "$source_parent"; mv "$work/source-parent" "$source_parent"
fonts="$payload/twres/fonts/ure"
while IFS= read -r file; do
    cp "$file" "$work/original"
    printf 'Corruption\n' >> "$file"
    refuse bash "$checker" "$payload"
    cp "$work/original" "$file"
done < <(find "$fonts" -type f | sort)
mkfifo "$fonts/unsupported-fifo"
refuse bash "$checker" "$payload"
refuse bash "$packager" "$tree" "$payload"
rm "$fonts/unsupported-fifo"
printf 'Unknown asset\n' > "$fonts/unknown.ttf"
refuse bash "$checker" "$payload"
refuse bash "$packager" "$tree" "$payload"
rm "$fonts/unknown.ttf"
mv "$payload/twres/fonts" "$work/payload-fonts"
ln -s "$work/payload-fonts" "$payload/twres/fonts"
refuse bash "$checker" "$payload"
refuse bash "$packager" "$tree" "$payload"
rm "$payload/twres/fonts"; mv "$work/payload-fonts" "$payload/twres/fonts"
cp "$device/localization/fonts.lock.tsv" "$work/original-table"
head -n 5 "$work/original-table" > "$device/localization/fonts.lock.tsv"
refuse bash "$packager" "$tree" "$payload"
refuse bash "$checker" "$payload"
cp "$work/original-table" "$device/localization/fonts.lock.tsv"
cp "$fixture/manifests/text-layout.lock.json" "$work/original-lock"
jq '.fonts.files[0].path="../outside.ttf"' "$work/original-lock" > "$fixture/manifests/text-layout.lock.json"
refuse bash "$checker" "$payload"
cp "$work/original-lock" "$fixture/manifests/text-layout.lock.json"
[[ $(snapshot "$payload") == "$before" ]]
bash "$checker" "$payload"
# Compare Android adapters with the exact original compilation lists.
sed -n 's/^#include "\([^"]*\.cc\)"$/src\/\1/p' \
    "$component/src/upstream/text-layout/harfbuzz-14.5.1/src/harfbuzz.cc" > "$work/hb-original-list"
cmp "$work/hb-original-list" "$component/configs/text-layout/harfbuzz-sources.list"
for name in harfbuzz fribidi; do
    sed -n '/^[[:space:]]*srcs: \[/,/^[[:space:]]*\],/s/^[[:space:]]*"\([^"]*\)",$/\1/p' \
        "$component/configs/text-layout/$name-Android.bp" > "$work/android-list"
    cmp "$component/configs/text-layout/$name-sources.list" "$work/android-list"
    if [[ $name == harfbuzz ]]; then
        sed -n '/^[[:space:]]*cflags: \[/,/^[[:space:]]*\],/s/^[[:space:]]*"\([^"]*\)",$/\1/p' \
            "$component/configs/text-layout/harfbuzz-Android.bp" > "$work/android-flags"
        cmp "$component/configs/text-layout/harfbuzz-flags.list" "$work/android-flags"
    fi
    git clone --shared --no-checkout "$component/referances/upstream/$name" "$fixture/referances/upstream/$name" > "$work/clone-$name.log" 2>&1
    pin=$(jq -r --arg name "$name" '.libraries[]|select(.name==$name)|.commit' "$fixture/manifests/text-layout.lock.json")
    git -C "$fixture/referances/upstream/$name" checkout --detach "$pin" >> "$work/clone-$name.log" 2>&1
done
cp --reflink=auto "$component/referances/upstream/fribidi-release/fribidi-1.0.17.tar.xz" "$fixture/referances/upstream/fribidi-release/"
bash "$fixture/scripts/prepare-android-text-layout.sh"
bash "$fixture/scripts/prepare-android-text-layout.sh"
# A previously reviewed FriBidi prefix may advance in both active snapshots.
# Unknown edits in either snapshot must still refuse before replacement.
fribidi_pin=$(jq -er '.libraries[]|select(.name=="fribidi")|.commit' "$fixture/manifests/text-layout.lock.json")
git -C "$fixture/referances/upstream/fribidi" show "$fribidi_pin:lib/fribidi-bidi.c" > "$work/fribidi-original.c"
mkdir -p "$work/fribidi-prefix/lib"
cp "$work/fribidi-original.c" "$work/fribidi-prefix/lib/fribidi-bidi.c"
patch --directory="$work/fribidi-prefix" -p1 --batch --fuzz=0 --no-backup-if-mismatch \
    < "$fixture/patches/0031-fribidi-allocation-failure.patch"
for predecessor in "$work/fribidi-original.c" "$work/fribidi-prefix/lib/fribidi-bidi.c"; do
    cp "$predecessor" "$fixture/src/upstream/text-layout/fribidi-1.0.17/lib/fribidi-bidi.c"
    cp "$predecessor" "$tree/external/ure-fribidi/lib/fribidi-bidi.c"
    bash "$fixture/scripts/prepare-android-text-layout.sh"
done
source="$fixture/src/upstream/text-layout/fribidi-1.0.17/lib/fribidi-bidi.c"
cp "$source" "$work/fribidi-reviewed.c"
printf '\n/* Unknown FriBidi source sentinel */\n' >> "$source"
source_before=$(snapshot "$fixture/src/upstream/text-layout")
refuse bash "$fixture/scripts/prepare-android-text-layout.sh"
[[ $(snapshot "$fixture/src/upstream/text-layout") == "$source_before" ]]
cp "$work/fribidi-reviewed.c" "$source"
source="$fixture/src/upstream/text-layout/harfbuzz-14.5.1/src/hb-common.cc"
cp "$source" "$work/original-source"
printf '\n// Unknown source sentinel\n' >> "$source"
source_before=$(snapshot "$fixture/src/upstream/text-layout")
refuse bash "$fixture/scripts/prepare-android-text-layout.sh"
[[ $(snapshot "$fixture/src/upstream/text-layout") == "$source_before" ]]
cp "$work/original-source" "$source"
executable="$fixture/src/upstream/text-layout/harfbuzz-14.5.1/.ci/build-win.sh"
permissions=$(stat -c %a "$executable")
chmod a-x "$executable"
refuse bash "$fixture/scripts/prepare-android-text-layout.sh"
[[ ! -x $executable ]]
chmod "$permissions" "$executable"
android_source="$tree/external/ure-fribidi/lib/fribidi-bidi.c"
cp "$android_source" "$work/original-source"
printf '\n/* Unknown Android source sentinel */\n' >> "$android_source"
source_before=$(snapshot "$tree/external/ure-fribidi")
refuse bash "$fixture/scripts/prepare-android-text-layout.sh"
[[ $(snapshot "$tree/external/ure-fribidi") == "$source_before" ]]
cp "$work/original-source" "$android_source"
bash "$device/prepare-text-notices.sh" "$tree" "$work/notices"
notices_before=$(snapshot "$work/notices")
bash "$device/prepare-text-notices.sh" "$tree" "$work/notices"
[[ $(snapshot "$work/notices") == "$notices_before" ]]
cmp "$tree/external/ure-harfbuzz/src/ms-use/COPYING" "$work/notices/harfbuzz-MS-USE.txt"
cmp "$device/text-notices/Unicode-LICENSE.txt" "$work/notices/unicode-data.txt"
rg -q 'Grigori Goronzy' "$work/notices/harfbuzz-source-notices.txt"
rg -q 'Dov Grobgeld' "$work/notices/fribidi-source-notices.txt"
mv "$work/notices/harfbuzz.txt" "$work/notice"
ln -s "$work/notice" "$work/notices/harfbuzz.txt"
refuse bash "$device/prepare-text-notices.sh" "$tree" "$work/notices"
[[ -L $work/notices/harfbuzz.txt && $(sha256sum "$work/notice" | cut -d' ' -f1) == "$(sha256sum "$tree/external/ure-harfbuzz/COPYING" | cut -d' ' -f1)" ]]
echo 'Text asset/notice, source-pin, Android-list and unknown-change controls passed; isolated records retained privately.'
