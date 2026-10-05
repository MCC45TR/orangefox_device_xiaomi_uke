#!/usr/bin/env bash
# Validate exact redistributed font bytes and accompanying license notices.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
payload=${1:?Staged or extracted ramdisk is required}
lock="$component/manifests/text-layout.lock.json"
[[ -d $payload && ! -L $payload ]]
[[ -f $lock && ! -L $lock ]]
jq -e '.schema_version==1 and (.fonts.files|length)==6 and
    all(.fonts.files[]; (.path|test("^[A-Za-z0-9_-]+/([0-9.]+/)?[A-Za-z0-9_-]+\\.(ttf|ttc)$")) and
        (.notice|test("^[A-Za-z0-9_-]+/NOTICE$")) and
        (.sha256|test("^[0-9a-f]{64}$")) and (.notice_sha256|test("^[0-9a-f]{64}$"))) and
    ([.fonts.files[].path|split("/")[-1]]|unique|length)==6 and
    ([.fonts.files[].notice|split("/")[0]]|unique|length)==6' "$lock" >/dev/null
for path in twres twres/fonts twres/fonts/ure; do
    [[ -d $payload/$path && ! -L $payload/$path ]]
done
fonts="$payload/twres/fonts/ure"
[[ -z $(find "$fonts" -mindepth 1 ! -type f -print -quit) ]]
[[ $(find "$fonts" -mindepth 1 -type f | wc -l) == 13 ]]
table="$component/src/device/xiaomi/uke/localization/fonts.lock.tsv"
[[ -f $table && ! -L $table && -f $fonts/FONT-SOURCES.tsv && ! -L $fonts/FONT-SOURCES.tsv ]]
# Two independently consumed locks must describe the same six assets.
cmp <(sed '/^#/d' "$table") <(jq -er '.fonts.files[]|[.sha256,.path,.notice,.notice_sha256]|@tsv' "$lock")
while IFS=$'\t' read -r asset digest notice notice_digest; do
    font="$fonts/${asset##*/}"
    license="$fonts/${notice%/*}-OFL.txt"
    [[ -f $font && ! -L $font && -f $license && ! -L $license ]]
    [[ $(sha256sum "$font" | cut -d' ' -f1) == "$digest" &&
       $(sha256sum "$license" | cut -d' ' -f1) == "$notice_digest" ]]
done < <(jq -er '.fonts.files[]|[.path,.sha256,.notice,.notice_sha256]|@tsv' "$lock")
cmp "$table" "$fonts/FONT-SOURCES.tsv"
echo 'Six exact Noto assets and notices verified; glyph and visual acceptance are separate.'
