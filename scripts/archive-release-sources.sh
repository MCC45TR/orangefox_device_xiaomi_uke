#!/usr/bin/env bash
# Create source snapshots with licenses, nested Magisk dependencies and build inputs.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "${BASH_SOURCE[0]}" "$@"
fi
source "$component/scripts/release-policy-lib.sh"
tree="$component/src/upstream/orangefox-android16"
candidate=${1:?Candidate name required}
[[ $# == 1 ]]
[[ $candidate =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$ ]]
destination="$component/artifacts/$candidate"
release_mutable_destination "$destination"
bash "$component/scripts/release-policy.sh" validate "$destination/RELEASE-POLICY.json"
gki="$component/../senemos-uke-kernel/referances/android/gki-stock-14529422"
magisk="$component/referances/tools/magiskboot-v26.5-vb-beta"
source_work=$(mktemp -d "$component/build/release-sources-XXXXXX")
trap 'rm -rf -- "$source_work"' EXIT
mkdir -p "$destination" "$source_work/build-inputs"
bash "$component/scripts/localization-evidence.sh" source > "$source_work/localization-before.json"
cmp "$source_work/localization-before.json" "$destination/LOCALIZATION-INPUTS.json"
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
sources=(build/soong build/blueprint bootable/recovery vendor/recovery vendor/twrp external/bash external/nano
    external/e2fsprogs external/f2fs-tools external/gptfdisk external/exfatprogs
    external/lzma external/magisk-prebuilt external/libncurses
    external/lz4 external/zlib external/zstd external/boringssl
    external/toybox external/selinux external/roboto-fonts external/noto-fonts external/freetype bionic system/core system/extras
    system/libbase system/libziparchive system/update_engine system/vold external/ntfs-3g external/jsoncpp external/libdrm)
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
            build/soong) bash "$component/scripts/prepare-reviewed-patches.sh" "$tree/$path" check soong;;
            build/blueprint) bash "$component/scripts/prepare-reviewed-patches.sh" "$tree/$path" check blueprint;;
            bootable/recovery) bash "$component/scripts/prepare-reviewed-patches.sh" "$tree/$path" check recovery
                cmp "$tree/$path/gui/ure.cpp" "$component/src/device/xiaomi/uke/ure-gui.cpp"
                cmp "$tree/$path/ure-write-gate.hpp" "$component/src/device/xiaomi/uke/ure-write-gate.hpp"
                cmp "$tree/$path/ure-lifecycle.hpp" "$component/src/device/xiaomi/uke/ure-lifecycle.hpp"
                cmp "$tree/$path/ure-readonly-fstab-import.hpp" "$component/src/device/xiaomi/uke/ure-readonly-fstab-import.hpp"
                cmp "$tree/$path/ure-telemetry.hpp" "$component/src/device/xiaomi/uke/ure-telemetry.hpp"
                cmp "$tree/$path/ure-clock.hpp" "$component/src/device/xiaomi/uke/ure-clock.hpp"
                cmp "$tree/$path/ure-theme.hpp" "$component/src/device/xiaomi/uke/ure-theme.hpp"
                for file in display-mirror.hpp display-mirror.cpp display-mirror-layout.cpp ure-text-layout.hpp ure-text-layout.cpp; do
                    cmp "$tree/$path/minuitwrp/$file" "$component/src/device/xiaomi/uke/$file"
                done;;
            system/core) bash "$component/scripts/prepare-reviewed-patches.sh" "$tree/$path" check fastboot;;
            system/vold) bash "$component/scripts/prepare-reviewed-patches.sh" "$tree/$path" check vold;;
            external/ntfs-3g) verify_reviewed_patch "$tree/$path" 0003-build-ntfsresize.patch;;
            external/zstd) verify_reviewed_patch "$tree/$path" 0009-native-boot-audit-codecs.patch;;
            external/freetype) bash "$component/scripts/prepare-reviewed-patches.sh" "$tree/$path" check freetype;;
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
# Immutable shaping libraries include their release-generated Unicode tables.
# This check rejects unreviewed source or build adapters before redistribution.
bash "$component/scripts/prepare-text-layout-sources.sh"
for name in harfbuzz fribidi; do
    version=$(jq -er --arg name "$name" '.libraries[]|select(.name==$name)|.version' "$component/manifests/text-layout.lock.json")
    text_source="$component/src/upstream/text-layout/$name-$version"
    diff -qr "$text_source" "$tree/external/ure-$name"
    tar -cf "$source_work/part.tar" -C "$component/src/upstream/text-layout" "$name-$version"
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
mkdir -p "$source_work/project/src" "$source_work/project/icon-sources"
origins="$component/src/device/xiaomi/uke/ui-icons/ORIGINS.json"
while read -r name expected; do
    svg="$component/referances/lucide-0.563.0/icons/$name.svg"
    [[ $(sha256sum "$svg" | cut -d' ' -f1) == "$expected" ]]
    cp -- "$svg" "$source_work/project/icon-sources/"
done < <(jq -r '.icons[]|"\(.name) \(.svg_sha256)"' "$origins")
cp -- "$component/referances/lucide-0.563.0/LICENSE" "$source_work/project/icon-sources/"
for path in device installer inventory host; do
    cp -a -- "$component/src/$path" "$source_work/project/src/"
done
# The inventoried owner font draft has no accepted adjacent redistribution
# notice and is not a production input. Exclude only the copied draft directory;
# preserve the developer's original files and the accepted source lock.
rm -rf -- "$source_work/project/src/device/xiaomi/uke/localization/fonts"
[[ ! -d $component/src/localization ]] || cp -a -- "$component/src/localization" "$source_work/project/src/"
cp -a -- "$component/patches" "$component/manifests" "$component/scripts" "$component/tests" "$source_work/project/"
cp -a -- "$component/configs" "$source_work/project/"
cp -- "$component/.gitattributes" "$source_work/project/"
cp -- "$component/LICENSE" "$component/docs/PRE-RELEASE.md" "$component/docs/HOST-TOOLS.md" "$source_work/project/"
cp -- "$component/docs/HOST-BUILD-BUDGET.md" "$source_work/project/"
cp -- "$component/docs/BUILD-COMPLETION.md" "$component/docs/RELEASE-POLICY.md" "$source_work/project/"
cp -- "$component/docs/LOCALIZATION-EVIDENCE.md" "$component/docs/MULTILINGUAL-TEXT.md" "$destination/LOCALIZATION-INPUTS.json" \
    "$destination/GUI-RESOURCE-INPUTS.json" "$source_work/project/"
cp -- "$component/docs/URE-NATIVE.md" "$component/docs/URE-NATIVE-CANDIDATE.md" "$source_work/project/"
cp -- "$component/docs/HOST-RESTORE.md" "$component/docs/PARTITION-MANAGER.md" "$source_work/project/"
cp -- "$component/docs/STOCK-IMAGE-RESTORE.md" "$source_work/project/"
cp -- "$component/docs/STOCK-BOOT-PREFLIGHT.md" "$source_work/project/"
cp -- "$component/docs/DISPLAY-SCALING.md" "$source_work/project/"
cp -- "$component/docs/RECOVERY-INTERFACE.md" "$component/docs/STOCK-VM-FIXTURE.md" "$component/docs/SENSOR-READINESS.md" "$source_work/project/"
if [[ -f $component/docs/FUNCTIONAL-VM-TESTS.md ]]; then cp -- "$component/docs/FUNCTIONAL-VM-TESTS.md" "$source_work/project/"; fi
cp -- "$component/docs/EXTERNAL-MONITOR.md" "$source_work/project/"
cp -- "$component/docs/TREE-BACKUP.md" "$source_work/project/"
cp -- "$component/docs/FILESYSTEM-MANAGER.md" "$component/docs/LINUX-RESCUE-AND-BOOT.md" "$component/docs/BTRFS-MANAGER.md" "$source_work/project/"
cp -- "$component/docs/BOOT-ROUTING.md" "$source_work/project/"
cp -- "$component/docs/RECOVERY-WRITE-POLICY.md" "$component/docs/WRITE-GATE-VM-TESTS.md" "$source_work/project/"
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
if [[ -f $component/reports/URE-PARTITION-LAYOUT-BUILD.md ]]; then
    cp -- "$component/reports/URE-PARTITION-LAYOUT-BUILD.md" "$source_work/project/PARTITION-LAYOUT-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-DISPLAY-BUILD.md ]]; then
    cp -- "$component/reports/URE-DISPLAY-BUILD.md" "$source_work/project/DISPLAY-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-EXTERNAL-DISPLAY-BUILD.md ]]; then
    cp -- "$component/reports/URE-EXTERNAL-DISPLAY-BUILD.md" "$source_work/project/EXTERNAL-DISPLAY-BUILD-REPORT.md"
fi
cp -a -- "$tree/out-public/target/product/uke/recovery/root/FFiles" "$source_work/project/payload-script-sources"
if [[ -f $component/reports/URE-TREE-BACKUP-BUILD.md ]]; then
    cp -- "$component/reports/URE-TREE-BACKUP-BUILD.md" "$source_work/project/TREE-BACKUP-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-RESCUE-FILESYSTEMS-BUILD.md ]]; then
    cp -- "$component/reports/URE-RESCUE-FILESYSTEMS-BUILD.md" "$source_work/project/RESCUE-FILESYSTEMS-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-PARTITION-JOB-BUILD.md ]]; then
    cp -- "$component/reports/URE-PARTITION-JOB-BUILD.md" "$source_work/project/PARTITION-JOB-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-STOCK-JOB-BUILD.md ]]; then
    cp -- "$component/reports/URE-STOCK-JOB-BUILD.md" "$source_work/project/STOCK-JOB-BUILD-REPORT.md"
fi
if [[ -f $component/reports/URE-STOCK-PREFLIGHT-BUILD.md ]]; then
    cp -- "$component/reports/URE-STOCK-PREFLIGHT-BUILD.md" "$source_work/project/STOCK-PREFLIGHT-BUILD-REPORT.md"
fi
tar -rf "$source_work/recovery.tar" -C "$source_work" project
bash "$component/scripts/localization-evidence.sh" source > "$source_work/localization-after.json"
cmp "$source_work/localization-before.json" "$source_work/localization-after.json"
gzip -n -1 -c "$source_work/recovery.tar" > "$destination/RECOVERY-UTILITY-SOURCES.tar.gz"
(cd -- "$destination" && sha256sum STOCK-GKI-SOURCE.tar.gz RECOVERY-UTILITY-SOURCES.tar.gz >> SHA256SUMS)
echo 'Stock GKI and recovery utility source snapshots archived with original licenses.'
