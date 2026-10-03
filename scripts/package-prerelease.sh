#!/usr/bin/env bash
# Package three distinct experimental assets. Never flash or open a block device.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
out="$tree/out-public"
product="$out/target/product/uke"
candidate=${1:-prerelease}
[[ $candidate =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$ ]]
destination="$component/artifacts/$candidate"
instructions="$component/docs/PRE-RELEASE.md"
if [[ $candidate != prerelease ]]; then instructions="$component/docs/URE-NATIVE-CANDIDATE.md"; fi
kernel="$component/build/stock-global/boot/kernel"
installer="$product/recovery/root/system/bin/uke-recovery-install"
mkboot="$out/host/linux-x86/bin/mkbootimg"
avb="$out/host/linux-x86/bin/avbtool"
for command in jq dd od zip sha256sum readelf strings; do command -v "$command" >/dev/null; done
for input in "$product/recovery.img" "$kernel" "$installer" "$mkboot" "$avb"; do
    [[ -s $input ]] || { echo "Missing build input: ${input##*/}" >&2; exit 1; }
done
[[ $(stat -c %s "$product/recovery.img") == 104857600 ]]
[[ $(sha256sum "$kernel" | cut -d' ' -f1) == $(jq -r .kernel_sha256 "$component/manifests/stock-kernel-source.json") ]]
[[ $(dd if="$product/recovery.img" bs=1 count=8 status=none) == 'ANDROID!' ]]
[[ $(od -An -tu4 -j8 -N4 "$product/recovery.img" | tr -d ' ') == 0 ]]
[[ $(od -An -tu4 -j40 -N4 "$product/recovery.img" | tr -d ' ') == 4 ]]
if readelf -l "$installer" | grep -q INTERP || readelf -d "$installer" | grep -q NEEDED; then
    echo 'Installer must be a self-contained static Android AArch64 executable' >&2; exit 1
fi
readelf -h "$installer" | grep -q 'Machine:.*AArch64'
"$component/tests/check-payload.sh" "$product/recovery/root"
"$component/tests/check-nested-payloads.sh" "$product/recovery/root"
mkdir -p "$destination"
package_work=$(mktemp -d "$component/build/package-XXXXXX")
trap 'rm -rf -- "$package_work"' EXIT
ramdisk_bytes=$(od -An -tu4 -j12 -N4 "$product/recovery.img" | tr -d ' ')
[[ $ramdisk_bytes =~ ^[0-9]+$ && $ramdisk_bytes -gt 0 && $ramdisk_bytes -lt 65000000 ]]
dd if="$product/recovery.img" of="$package_work/ramdisk" bs=1M iflag=skip_bytes,count_bytes skip=4096 count="$ramdisk_bytes" status=none
cp -- "$product/recovery.img" "$destination/OrangeFox-uke-recovery.img"
"$mkboot" --header_version 4 --kernel "$kernel" --ramdisk "$package_work/ramdisk" \
    --os_version 16.0.0 --os_patch_level 2025-06 \
    --cmdline 'androidboot.force_normal_boot=0' --output "$destination/OrangeFox-uke-fastboot-boot.img"
"$avb" add_hash_footer --image "$destination/OrangeFox-uke-fastboot-boot.img" \
    --partition_name boot --partition_size 100663296 --algorithm NONE \
    --salt "$(jq -r .kernel_sha256 "$component/manifests/stock-kernel-source.json")"
[[ $(od -An -tu4 -j8 -N4 "$destination/OrangeFox-uke-fastboot-boot.img" | tr -d ' ') == 35432960 ]]
[[ $(od -An -tu4 -j12 -N4 "$destination/OrangeFox-uke-fastboot-boot.img" | tr -d ' ') == "$ramdisk_bytes" ]]
cp -- "$installer" "$package_work/uke-recovery-install"
cp -- "$product/recovery.img" "$package_work/recovery.img"
mkdir -p "$package_work/META-INF/com/google/android"
cp -- "$component/src/installer/update-binary" "$package_work/META-INF/com/google/android/update-binary"
chmod 755 "$package_work/META-INF/com/google/android/update-binary" "$package_work/uke-recovery-install"
sha256sum "$product/recovery.img" | cut -d' ' -f1 > "$package_work/recovery.sha256"
cp -- "$instructions" "$package_work/INSTALL.md"
cp -- "$component/docs/PRE-RELEASE.md" "$package_work/STOCK-RETURN.md"
cp -- "$component/src/device/xiaomi/uke/ure-tools.lock.json" "$package_work/URE-TOOLS.json"
cp -- "$component/docs/URE-NATIVE.md" "$package_work/URE-NATIVE.md"
cp -- "$component/docs/HOST-RESTORE.md" "$package_work/HOST-RESTORE.md"
cp -- "$component/docs/PARTITION-MANAGER.md" "$package_work/PARTITION-MANAGER.md"
cp -- "$component/docs/STOCK-IMAGE-RESTORE.md" "$package_work/STOCK-IMAGE-RESTORE.md"
cp -- "$component/docs/DISPLAY-SCALING.md" "$package_work/DISPLAY-SCALING.md"
cp -- "$component/docs/EXTERNAL-MONITOR.md" "$package_work/EXTERNAL-MONITOR.md"
cp -- "$component/docs/TREE-BACKUP.md" "$package_work/TREE-BACKUP.md"
for guide in FILESYSTEM-MANAGER LINUX-RESCUE-AND-BOOT BTRFS-MANAGER; do
    cp -- "$component/docs/$guide.md" "$package_work/$guide.md"
done
cp -- "$component/LICENSE" "$package_work/LICENSE"
# All files receive a deterministic timestamp. Replacing this generated asset is intentional.
find "$package_work" -type f -exec touch -d '@1790726400' {} +
zipfile="$destination/OrangeFox-uke-flashable.zip"
[[ ! -e $zipfile ]] || mv -- "$zipfile" "$package_work/previous.zip"
(cd -- "$package_work" && zip -X -9 "$zipfile" META-INF/com/google/android/update-binary \
    recovery.img uke-recovery-install recovery.sha256 INSTALL.md STOCK-RETURN.md URE-TOOLS.json URE-NATIVE.md HOST-RESTORE.md PARTITION-MANAGER.md STOCK-IMAGE-RESTORE.md DISPLAY-SCALING.md EXTERNAL-MONITOR.md TREE-BACKUP.md FILESYSTEM-MANAGER.md LINUX-RESCUE-AND-BOOT.md BTRFS-MANAGER.md LICENSE >/dev/null)
unzip -t "$zipfile" >/dev/null
cp -- "$component/manifests/orangefox-android16-uke.lock.xml" "$destination/ORANGEFOX-SOURCE-PINS.xml"
cp -- "$component/manifests/stock-kernel-source.json" "$destination/STOCK-KERNEL-SOURCE.json"
cp -- "$instructions" "$destination/INSTALL.md"
cp -- "$component/docs/PRE-RELEASE.md" "$destination/STOCK-RETURN.md"
cp -- "$component/src/device/xiaomi/uke/ure-tools.lock.json" "$destination/URE-TOOLS.json"
(cd -- "$destination" && sha256sum OrangeFox-uke-fastboot-boot.img OrangeFox-uke-recovery.img \
    OrangeFox-uke-flashable.zip ORANGEFOX-SOURCE-PINS.xml STOCK-KERNEL-SOURCE.json INSTALL.md > SHA256SUMS)
echo 'Three experimental assets packaged; no device boot or flash has been performed.'
