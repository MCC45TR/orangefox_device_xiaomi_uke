#!/usr/bin/env bash
# Index consumed files and filesystem layout, including untracked/ignored inputs.
set -euo pipefail
export LC_ALL=C
umask 077
root=${1:?Source root required}
destination=${2:?New private index directory required}
kind=${3:?android|project|products|payload|localization|ui}
[[ -d $root && ! -L $root && ! -e $destination ]]
mkdir -m 0700 -- "$destination"
destination=$(realpath -e -- "$destination")
cd -- "$root"
root=$(pwd -P)
case $kind in
    android) roots=(.);;
    project)
        roots=(.gitattributes LICENSE configs patches manifests scripts tests src/device src/installer src/inventory)
        [[ ! -d src/host ]] || roots+=(src/host)
        [[ ! -d src/localization ]] || roots+=(src/localization)
        ;;
    products)
        roots=(target/product/uke/recovery.img target/product/uke/recovery/root target/product/uke/system host/linux-x86/bin
            soong/soong.twrp_uke.variables soong/soong.twrp_uke.extra.variables soong/soong.environment.available build-twrp_uke.ninja)
        [[ ! -d host/linux-x86/lib64 ]] || roots+=(host/linux-x86/lib64)
        # Match the six prefixes searched by the pinned libcutils fs_config.
        for partition in system vendor oem odm product system_ext; do
            for config in files dirs; do
                path="target/product/uke/$partition/etc/fs_config_$config"
                if [[ -e $path || -L $path ]]; then
                    [[ -f $path && ! -L $path && $(realpath -e -- "$path") == "$root/$path" ]] || {
                        echo 'External or nonregular product filesystem configuration rejected.' >&2; exit 1;
                    }
                    [[ $partition == system ]] || roots+=("$path")
                fi
            done
        done
        fixture=soong/.intermediates/device/xiaomi/uke/recoveryctl/uke-btrfs-vm-fixture
        [[ ! -d $fixture ]] || roots+=("$fixture")
        ;;
    payload) roots=(.);;
    localization)
        source "$root/scripts/localization-evidence-lib.sh"
        localization_config configs/localization-inputs.json
        mapfile -t roots < <(jq -r '.required[]' configs/localization-inputs.json)
        while IFS= read -r path; do
            [[ ! -e $path && ! -L $path ]] || roots+=("$path")
        done < <(jq -r '.optional[]' configs/localization-inputs.json)
        ;;
    ui) roots=(twres sbin/maintainer.xml system/etc/ure/licenses);;
    *) exit 2;;
esac
for path in "${roots[@]}"; do [[ -e $path || -L $path ]]; done
# Android's previous output trees and VCS bookkeeping are not compiler inputs.
# Actual source revisions are recorded separately; every remaining file,
# including prebuilts and staged non-Git sources, is hashed by content.
timestamp=-
[[ $kind != android && $kind != project ]] || timestamp='%T@'
prunes=(-name .git -o -path './.repo' -o -path './out' -o -path './out-clean' -o -path './out-public')
case $kind in products|payload|ui) prunes=(-false);; esac
find "${roots[@]}" \( "${prunes[@]}" \) -prune \
    -o \( ! -path . \( -type d -printf '%p\td\t%m\t-\t\0' \
            -o -printf "%p\t%y\t%m\t$timestamp\t%l\0" \) \) | sort -z -S 64M --parallel=1 > "$destination/layout.bin"
awk -v RS='\0' -F '\t' 'NF!=5 || $1~/[\n\r]/ || $2!~/^[fdl]$/ || $3!~/^[0-7]{3,4}$/ ||
    $4!~/^(-|[0-9]+\.[0-9]+)$/ || $5~/[\n\r]/ {exit 1}' "$destination/layout.bin" || {
    echo 'Ambiguous path, symlink target or special source object refused.' >&2; exit 1;
}
: > "$destination/external-links.tsv"
if [[ $kind == android || $kind == project || $kind == localization ]]; then
    awk -v RS='\0' -F '\t' '$2=="l" {printf "%s%c",$1,0}' "$destination/layout.bin" > "$destination/links.tmp"
    while IFS= read -r -d '' path; do
        if [[ $kind == localization ]]; then
            resolved=$(realpath -e -- "$path")
            [[ -f $resolved && ! -L $resolved ]] || {
                echo 'Unreviewed directory or unresolved localization link refused.' >&2; exit 1;
            }
            printf '%s\t%s\t%s\n' "$path" "$(sha256sum "$resolved" | cut -d' ' -f1)" "$(stat -c '%a' "$resolved")" >> "$destination/external-links.tsv"
            continue
        fi
        if resolved=$(realpath -e -- "$path" 2>/dev/null); then
            case $resolved in
                "$root/out"|"$root/out/"*|"$root/out-clean"|"$root/out-clean/"*|"$root/out-public"|"$root/out-public/"*|"$root/.repo/"*)
                    echo 'A source symlink resolves into excluded output or VCS storage.' >&2; exit 1;;
                "$root/"*) :;;
                *)
                    [[ -f $resolved && ! -L $resolved ]] || { echo 'An unreviewed source symlink escapes into a host directory.' >&2; exit 1; }
                    printf '%s\t%s\t%s\n' "$path" "$(sha256sum "$resolved" | cut -d' ' -f1)" "$(stat -c '%a:%y' "$resolved")" >> "$destination/external-links.tsv";;
            esac
        fi
    done < "$destination/links.tmp"
    rm -- "$destination/links.tmp"
fi
find "${roots[@]}" \( "${prunes[@]}" \) -prune \
    -o -type f -print0 | sort -z -S 64M --parallel=1 | xargs -0 -r sha256sum > "$destination/files.sha256"
[[ -s $destination/files.sha256 ]]
sha256sum "$destination/files.sha256" "$destination/layout.bin" "$destination/external-links.tsv" | awk '{print $1}' > "$destination/index.sha256"
