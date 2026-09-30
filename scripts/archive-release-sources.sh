#!/usr/bin/env bash
# Create source snapshots with licenses, nested Magisk dependencies and build inputs.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
destination="$component/artifacts/prerelease"
gki="$component/../senemos-uke-kernel/referances/android/gki-stock-14529422"
magisk="$component/referances/tools/magiskboot-v26.5-vb-beta"
source_work=$(mktemp -d "$component/build/release-sources-XXXXXX")
trap 'rm -rf -- "$source_work"' EXIT
mkdir -p "$destination" "$source_work/build-inputs"
[[ $(git -C "$gki" rev-parse HEAD) == $(jq -r .source_commit "$component/manifests/stock-kernel-source.json") ]]
git -C "$gki" archive --format=tar --prefix=kernel/ HEAD > "$source_work/gki.tar"
kernel="$component/build/stock-global/boot/kernel"
start=$(LC_ALL=C grep -abo 'IKCFG_ST' "$kernel" | cut -d: -f1)
end=$(LC_ALL=C grep -abo 'IKCFG_ED' "$kernel" | cut -d: -f1)
[[ $start =~ ^[0-9]+$ && $end =~ ^[0-9]+$ && $end -gt $((start+8)) ]]
dd if="$kernel" bs=1M iflag=skip_bytes,count_bytes skip="$((start+8))" count="$((end-start-8))" status=none \
    | gzip -dc > "$source_work/build-inputs/stock-gki.config"
cp -- "$component/manifests/stock-kernel-source.json" "$source_work/build-inputs/"
tar -rf "$source_work/gki.tar" -C "$source_work" build-inputs
gzip -n -1 -c "$source_work/gki.tar" > "$destination/STOCK-GKI-SOURCE.tar.gz"

[[ $(git -C "$magisk" rev-parse HEAD) == 358ecafba26b5ec54ae05f3b47e4ab00cf978632 ]]
if git -C "$magisk" submodule status --recursive | grep -E '^[-+U]'; then
    echo 'Magisk source dependencies are incomplete or mismatched' >&2; exit 1
fi
git -C "$magisk" archive --format=tar --prefix=magisk/ HEAD > "$source_work/recovery.tar"
while IFS= read -r dependency; do
    git -C "$magisk/$dependency" archive --format=tar --prefix="magisk/$dependency/" HEAD > "$source_work/nested.tar"
    tar --concatenate --file="$source_work/recovery.tar" "$source_work/nested.tar"
done < <(git -C "$magisk" submodule foreach --quiet --recursive 'printf "%s\n" "$displaypath"')

# GPL/LGPL utilities plus their dependency sources. Per-file original licenses
# remain in these archives. The complete Android project lock is also included.
sources=(bootable/recovery vendor/recovery vendor/twrp external/bash external/nano
    external/e2fsprogs external/f2fs-tools external/gptfdisk external/exfatprogs
    external/lzma external/magisk-prebuilt external/libncurses
    external/lz4 external/zlib external/zstd external/boringssl
    external/toybox external/selinux external/roboto-fonts bionic system/core system/extras
    system/libbase system/libziparchive system/update_engine)
for path in "${sources[@]}"; do
    [[ -d "$tree/$path/.git" || -f "$tree/$path/.git" ]] || { echo "Missing source: $path" >&2; exit 1; }
    [[ -z $(git -C "$tree/$path" status --porcelain --untracked-files=no) ]] || {
        echo "Unexpected modification in source: $path" >&2; exit 1;
    }
    exclusions=()
    case "$path" in
        bootable/recovery) exclusions=(':(exclude)gui/theme/common/fonts/*.ttf');;
        vendor/recovery) exclusions=(':(exclude)prebuilt' ':(exclude)installer' ':(exclude)Files/*.ttf' ':(exclude)Files/*.zip');;
    esac
    git -C "$tree/$path" archive --format=tar --prefix="android/$path/" HEAD . "${exclusions[@]}" > "$source_work/part.tar"
    tar --concatenate --file="$source_work/recovery.tar" "$source_work/part.tar"
done
mkdir -p "$source_work/project/src"
for path in device installer inventory; do
    cp -a -- "$component/src/$path" "$source_work/project/src/"
done
cp -a -- "$component/patches" "$component/manifests" "$component/scripts" "$component/tests" "$source_work/project/"
cp -- "$component/LICENSE" "$component/docs/PRE-RELEASE.md" "$component/docs/HOST-TOOLS.md" "$source_work/project/"
cp -a -- "$tree/out-public/target/product/uke/recovery/root/FFiles" "$source_work/project/payload-script-sources"
tar -rf "$source_work/recovery.tar" -C "$source_work" project
gzip -n -1 -c "$source_work/recovery.tar" > "$destination/RECOVERY-UTILITY-SOURCES.tar.gz"
(cd -- "$destination" && sha256sum STOCK-GKI-SOURCE.tar.gz RECOVERY-UTILITY-SOURCES.tar.gz >> SHA256SUMS)
echo 'Stock GKI and recovery utility source snapshots archived with original licenses.'
