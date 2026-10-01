#!/usr/bin/env bash
# Create source snapshots with licenses, nested Magisk dependencies and build inputs.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
candidate=${1:-prerelease}
[[ $candidate =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$ ]]
destination="$component/artifacts/$candidate"
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
    system/libbase system/libziparchive system/update_engine external/ntfs-3g external/jsoncpp)
verify_reviewed_patch() {
    local source_path=$1; shift
    local index="$source_work/verification-index" file expected actual
    [[ ! -e $index ]] || { echo 'Unexpected temporary verification index' >&2; exit 1; }
    GIT_INDEX_FILE="$index" git -C "$source_path" read-tree HEAD
    for file in "$@"; do GIT_INDEX_FILE="$index" git -C "$source_path" apply --cached --unidiff-zero "$component/patches/$file"; done
    GIT_INDEX_FILE="$index" git -C "$source_path" diff --cached --name-only HEAD > "$source_work/expected-paths"
    git -C "$source_path" diff --name-only HEAD > "$source_work/actual-paths"
    cmp "$source_work/expected-paths" "$source_work/actual-paths"
    while IFS= read -r file; do
        expected=$(GIT_INDEX_FILE="$index" git -C "$source_path" show ":$file" | sha256sum | cut -d' ' -f1)
        actual=$(sha256sum "$source_path/$file" | cut -d' ' -f1)
        [[ $expected == "$actual" ]] || { echo 'Active source differs from reviewed patches' >&2; exit 1; }
    done < "$source_work/expected-paths"
    unlink -- "$index"
}
for path in "${sources[@]}"; do
    [[ -d "$tree/$path/.git" || -f "$tree/$path/.git" ]] || { echo "Missing source: $path" >&2; exit 1; }
    if [[ -n $(git -C "$tree/$path" status --porcelain --untracked-files=no) ]]; then
        case "$path" in
            bootable/recovery) verify_reviewed_patch "$tree/$path" 0002-native-ure-ui.patch 0004-link-native-ure.patch 0006-tablet-interface-density.patch
                cmp "$tree/$path/gui/ure.cpp" "$component/src/device/xiaomi/uke/ure-gui.cpp";;
            external/ntfs-3g) verify_reviewed_patch "$tree/$path" 0003-build-ntfsresize.patch;;
            vendor/recovery) verify_reviewed_patch "$tree/$path" 0005-propagate-callback-failure.patch;;
            *) echo "Unexpected modification in source: $path" >&2; exit 1;;
        esac
    fi
    exclusions=()
    case "$path" in
        bootable/recovery) exclusions=(':(exclude)gui/theme/common/fonts/*.ttf');;
        vendor/recovery) exclusions=(':(exclude)prebuilt' ':(exclude)installer' ':(exclude)Files/*.ttf' ':(exclude)Files/*.zip');;
    esac
    git -C "$tree/$path" archive --format=tar --prefix="android/$path/" HEAD . "${exclusions[@]}" > "$source_work/part.tar"
    tar --concatenate --file="$source_work/recovery.tar" "$source_work/part.tar"
done
# Preserve pristine upstream snapshots and the adapters that reconstruct the
# actual source build, including original component/dependency licenses.
for tool in wimlib dropbear; do
    reference="$component/referances/upstream/$tool"
    [[ -z $(git -C "$reference" status --porcelain --untracked-files=no) ]]
    git -C "$reference" archive --format=tar --prefix="ure-upstream/$tool/" HEAD > "$source_work/part.tar"
    tar --concatenate --file="$source_work/recovery.tar" "$source_work/part.tar"
done
mkdir -p "$source_work/project/src"
for path in device installer inventory; do
    cp -a -- "$component/src/$path" "$source_work/project/src/"
done
cp -a -- "$component/patches" "$component/manifests" "$component/scripts" "$component/tests" "$source_work/project/"
cp -a -- "$component/configs" "$source_work/project/"
cp -- "$component/LICENSE" "$component/docs/PRE-RELEASE.md" "$component/docs/HOST-TOOLS.md" "$source_work/project/"
cp -- "$component/docs/URE-NATIVE.md" "$component/docs/URE-NATIVE-CANDIDATE.md" "$source_work/project/"
cp -- "$component/docs/HOST-RESTORE.md" "$component/docs/PARTITION-MANAGER.md" "$source_work/project/"
cp -- "$component/docs/DISPLAY-SCALING.md" "$source_work/project/"
cp -- "$component/docs/COMPREHENSIVE-ROADMAP.md" "$component/docs/FEATURE-PARITY.md" \
    "$component/docs/ARCHITECTURE.md" "$source_work/project/"
cp -- "$component/reports/URE-NATIVE-BUILD.md" "$source_work/project/BUILD-REPORT.md"
if [[ -f $component/reports/URE-STREAMING-BUILD.md ]]; then
    cp -- "$component/reports/URE-STREAMING-BUILD.md" "$source_work/project/STREAMING-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-GPT-BUILD.md ]]; then
    cp -- "$component/reports/URE-GPT-BUILD.md" "$source_work/project/GPT-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-STORAGE-BUILD.md ]]; then
    cp -- "$component/reports/URE-STORAGE-BUILD.md" "$source_work/project/STORAGE-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-RESTORE-BUILD.md ]]; then
    cp -- "$component/reports/URE-RESTORE-BUILD.md" "$source_work/project/RESTORE-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-HOST-RESTORE-BUILD.md ]]; then
    cp -- "$component/reports/URE-HOST-RESTORE-BUILD.md" "$source_work/project/HOST-RESTORE-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-STOCK-GPT-BUILD.md ]]; then
    cp -- "$component/reports/URE-STOCK-GPT-BUILD.md" "$source_work/project/STOCK-GPT-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-PARTITION-MAP-BUILD.md ]]; then
    cp -- "$component/reports/URE-PARTITION-MAP-BUILD.md" "$source_work/project/PARTITION-MAP-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-DISPLAY-BUILD.md ]]; then
    cp -- "$component/reports/URE-DISPLAY-BUILD.md" "$source_work/project/DISPLAY-BUILD-REPORT.md"
fi
cp -a -- "$tree/out-public/target/product/uke/recovery/root/FFiles" "$source_work/project/payload-script-sources"
tar -rf "$source_work/recovery.tar" -C "$source_work" project
gzip -n -1 -c "$source_work/recovery.tar" > "$destination/RECOVERY-UTILITY-SOURCES.tar.gz"
(cd -- "$destination" && sha256sum STOCK-GKI-SOURCE.tar.gz RECOVERY-UTILITY-SOURCES.tar.gz >> SHA256SUMS)
echo 'Stock GKI and recovery utility source snapshots archived with original licenses.'
