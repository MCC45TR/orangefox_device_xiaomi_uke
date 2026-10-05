#!/usr/bin/env bash
# Host-only packaging helper; copy original licensed font bytes and notices.
set -euo pipefail
export LC_ALL=C
source_root=${1:?Android source root is required}
payload=${2:?Staged ramdisk is required}
device=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ ! -L $payload && -d $source_root && ! -L $source_root ]]
lock="$device/localization/fonts.lock.tsv"
[[ -f $lock && ! -L $lock ]]
# Check every existing child component before any copy. A regular leaf reached
# through a symlink is still outside the reviewed source/payload namespace.
regular_path() {
    local root=$1 relative=$2 child=$1 segment
    local -a segments
    IFS=/ read -r -a segments <<< "$relative"
    for segment in "${segments[@]}"; do
        child="$child/$segment"
        [[ ! -L $child && ( ! -e $child || -d $child || -f $child ) ]] || return 1
    done
    [[ $child == "$root/"* ]]
}
declare -A destinations=()
declare -a records=()
count=0
while IFS=$'\t' read -r digest asset notice notice_digest extra; do
    [[ $digest != '#'* ]] || continue
    [[ -z $extra && $digest =~ ^[0-9a-f]{64}$ && $notice_digest =~ ^[0-9a-f]{64}$ ]]
    [[ $asset =~ ^[A-Za-z0-9_-]+/([0-9.]+/)?[A-Za-z0-9_-]+\.(ttf|ttc)$ &&
       $notice =~ ^[A-Za-z0-9_-]+/NOTICE$ ]]
    source="$source_root/external/noto-fonts/$asset"
    license="$source_root/external/noto-fonts/$notice"
    regular_path "$source_root" "external/noto-fonts/$asset"
    regular_path "$source_root" "external/noto-fonts/$notice"
    [[ -f $source && ! -L $source && -f $license && ! -L $license ]]
    [[ $(sha256sum "$source" | cut -d' ' -f1) == "$digest" &&
       $(sha256sum "$license" | cut -d' ' -f1) == "$notice_digest" ]]
    destination="$payload/twres/fonts/ure/${asset##*/}"
    license_destination="$payload/twres/fonts/ure/${notice%/*}-OFL.txt"
    regular_path "$payload" "twres/fonts/ure/${asset##*/}"
    regular_path "$payload" "twres/fonts/ure/${notice%/*}-OFL.txt"
    [[ ! ${destinations[${asset##*/}]+present} && ! ${destinations[${notice%/*}-OFL.txt]+present} ]]
    destinations[${asset##*/}]=1
    destinations[${notice%/*}-OFL.txt]=1
    records+=("$digest" "$asset" "$notice" "$notice_digest")
    ((count+=1))
done < "$lock"
[[ $count == 6 ]]
regular_path "$payload" twres/fonts/ure/FONT-SOURCES.tsv
if [[ -d $payload/twres/fonts/ure ]]; then
    while IFS= read -r -d '' path; do
        [[ -f $path && ! -L $path && ( ${path##*/} == FONT-SOURCES.tsv || ${destinations[${path##*/}]+present} ) ]]
    done < <(find "$payload/twres/fonts/ure" -mindepth 1 -print0)
fi
mkdir -p "$payload/twres/fonts/ure"
for ((index=0;index<${#records[@]};index+=4)); do
    digest=${records[index]}; asset=${records[index+1]}
    notice=${records[index+2]}; notice_digest=${records[index+3]}
    destination="$payload/twres/fonts/ure/${asset##*/}"
    license_destination="$payload/twres/fonts/ure/${notice%/*}-OFL.txt"
    cmp -s -- "$source_root/external/noto-fonts/$asset" "$destination" || cp -- "$source_root/external/noto-fonts/$asset" "$destination"
    cmp -s -- "$source_root/external/noto-fonts/$notice" "$license_destination" || cp -- "$source_root/external/noto-fonts/$notice" "$license_destination"
    [[ $(sha256sum "$destination" | cut -d' ' -f1) == "$digest" &&
       $(sha256sum "$license_destination" | cut -d' ' -f1) == "$notice_digest" ]]
done
cmp -s -- "$lock" "$payload/twres/fonts/ure/FONT-SOURCES.tsv" || cp -- "$lock" "$payload/twres/fonts/ure/FONT-SOURCES.tsv"
echo 'Six unchanged Noto font assets and their exact OFL notices installed.'
