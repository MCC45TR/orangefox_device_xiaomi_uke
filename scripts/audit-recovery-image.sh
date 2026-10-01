#!/usr/bin/env bash
# Extract and audit the actual header-v4 LZ4 ramdisk, never only staging files.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
image=$(realpath -- "${1:?Usage: audit-recovery-image.sh IMAGE REPORT_JSON [--qemu]}")
report=${2:?Missing output report}
mode=${3:-}
[[ -z $mode || $mode == --qemu ]]
tree="$component/src/upstream/orangefox-android16"
lz4="$tree/out-public/host/linux-x86/bin/lz4"
[[ -f $image && ! -L $image && -x $lz4 ]]
[[ $(dd if="$image" bs=1 count=8 status=none) == 'ANDROID!' ]]
[[ $(od -An -tu4 -j8 -N4 "$image" | tr -d ' ') == 0 ]]
[[ $(od -An -tu4 -j40 -N4 "$image" | tr -d ' ') == 4 ]]
bytes=$(od -An -tu4 -j12 -N4 "$image" | tr -d ' ')
[[ $bytes =~ ^[0-9]+$ && $bytes -gt 0 && $bytes -le 65000000 ]]
work=$(mktemp -d "$component/build/image-audit-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
dd if="$image" of="$work/ramdisk.lz4" bs=1M iflag=skip_bytes,count_bytes skip=4096 count="$bytes" status=none
"$lz4" -dc "$work/ramdisk.lz4" > "$work/ramdisk.cpio"
[[ $(stat -c %s "$work/ramdisk.cpio") -le 268435456 ]]
cpio -it --quiet < "$work/ramdisk.cpio" > "$work/entries"
[[ $(wc -l < "$work/entries") -le 20000 ]]
while IFS= read -r entry; do
    case "$entry" in /*|../*|*/../*|*/..|*\\*|*$'\r'*) echo 'Unsafe ramdisk member rejected' >&2; exit 1;; esac
done < "$work/entries"
mkdir "$work/root"
# Absolute runtime symlinks are expected. Read-only host bindings make an
# attempted extraction through a symlink outside /mnt fail safely.
bwrap --ro-bind / / --bind "$work/root" /mnt --chdir /mnt \
    cpio -idm --quiet --no-absolute-filenames < "$work/ramdisk.cpio"
bash "$component/tests/check-payload.sh" "$work/root"
bash "$component/tests/check-nested-payloads.sh" "$work/root"
bash "$component/tests/check-elf-closure.sh" "$work/root"
xmllint --noout "$work/root/sbin/maintainer.xml" "$work/root/twres/pages/advanced.xml"
cmp "$component/src/device/xiaomi/uke/maintainer.xml" "$work/root/sbin/maintainer.xml"
cmp "$component/src/device/xiaomi/uke/ure-tools.lock.json" "$work/root/system/etc/ure/tools.lock.json"
cmp "$tree/out-public/target/product/uke/system/etc/mke2fs.conf" "$work/root/system/etc/mke2fs.conf"
grep -q 'page">ure_home<' "$work/root/twres/pages/advanced.xml"
[[ $(readlink -- "$work/root/system/bin/dropbearkey") == /system/bin/dropbear ]]
[[ ! -e $work/root/system/bin/keystore_cli_v2 ]]
for tool in uke-recoveryctl uke-recovery-install recovery dropbear wimlib-imagex ntfsresize fsck.exfat dump.exfat mkfs.exfat fsck.f2fs; do
    cmp "$tree/out-public/target/product/uke/recovery/root/system/bin/$tool" "$work/root/system/bin/$tool"
done
qemu=false
if [[ $mode == --qemu ]]; then
    if [[ ${URE_AUDIT_TRACE:-0} == 1 ]]; then
        bash -x "$component/tests/check-aarch64.sh" "$work/root"
    else
        bash "$component/tests/check-aarch64.sh" "$work/root"
    fi
    qemu=true
fi
jq -n --arg image "$(sha256sum "$image" | cut -d' ' -f1)" \
    --arg ramdisk "$(sha256sum "$work/ramdisk.lz4" | cut -d' ' -f1)" \
    --arg cli "$(sha256sum "$work/root/system/bin/uke-recoveryctl" | cut -d' ' -f1)" \
    --arg runner "$(sha256sum "$component/tests/check-aarch64.sh" | cut -d' ' -f1)" \
    --arg auditor "$(sha256sum "$component/scripts/audit-recovery-image.sh" | cut -d' ' -f1)" \
    --argjson bytes "$bytes" --argjson qemu "$qemu" \
    '{schema_version:1,recovery_image_sha256:$image,compressed_ramdisk:{bytes:$bytes,sha256:$ramdisk},native_cli_sha256:$cli,aarch64_runner_sha256:$runner,auditor_sha256:$auditor,validation:{extracted_ramdisk:true,payload_privacy:true,no_python_payload:true,recursive_zip_scan:true,elf_dependency_closure:true,gui_xml:true,tool_manifest:true,staged_target_binaries_match:true,qemu_user_fixtures:$qemu,physical_device:false,gui_rendering:false,hardware_rollback:false}}' > "$report"
echo 'Final compressed ramdisk audit passed; source, emulation and hardware evidence remain separate.'
