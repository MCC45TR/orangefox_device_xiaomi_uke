#!/usr/bin/env bash
# Extract and audit the actual header-v4 LZ4 ramdisk, never only staging files.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
image=$(realpath -- "${1:?Usage: audit-recovery-image.sh IMAGE REPORT_JSON [--qemu]}")
report=${2:?Missing output report}
source "$component/scripts/release-policy-lib.sh"
release_output_mutable "$component" "$report"
mode=${3:-}
[[ -z $mode || $mode == --qemu ]]
tree="$component/src/upstream/orangefox-android16"
bash "$component/scripts/build-evidence.sh" verify
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
bash "$component/scripts/build-evidence.sh" payload "$image" "$work/root" "$work/compile-evidence.json"
bash "$component/scripts/localization-evidence.sh" source > "$work/localization.json"
bash "$component/scripts/localization-evidence.sh" ui "$work/root" > "$work/extracted-ui.json"
bash "$component/scripts/localization-evidence.sh" ui "$tree/out-public/target/product/uke/recovery/root" > "$work/staged-ui.json"
bash "$component/scripts/check-ui-resource-parity.sh" \
    "$tree/out-public/target/product/uke/recovery/root" "$work/root" "$work/ui-comparison" > "$work/ui-comparison.json"
cmp "$work/extracted-ui.json" "$work/ui-comparison/extracted-ui.json"
cmp "$work/staged-ui.json" "$work/ui-comparison/staged-ui.json"
bash "$component/scripts/check-font-resources.sh" "$work/root"
bash "$component/tests/check-payload.sh" "$work/root"
bash "$component/tests/check-nested-payloads.sh" "$work/root"
bash "$component/tests/check-elf-closure.sh" "$work/root"
xmllint --noout "$work/root/sbin/maintainer.xml" "$work/root/twres/pages/advanced.xml"
cmp "$component/src/device/xiaomi/uke/maintainer.xml" "$work/root/sbin/maintainer.xml"
cmp "$component/src/device/xiaomi/uke/ure-gui.cpp" "$tree/bootable/recovery/gui/ure.cpp"
for header in ure-localization.hpp ure-locale-keys.hpp; do
    cmp "$component/src/device/xiaomi/uke/$header" "$tree/bootable/recovery/$header"
done
while IFS= read -r -d '' source_file; do
    cmp -- "$source_file" "$tree/device/xiaomi/uke/${source_file#"$component/src/device/xiaomi/uke/"}"
done < <(find "$component/src/device/xiaomi/uke" -type f -print0)
rg -q 'type == "urepartitionmap"' "$tree/bootable/recovery/gui/pages.cpp"
[[ $(xmllint --xpath 'count(/recovery/pages/page[@name="ure_layout"]//urepartitionmap)' "$work/root/sbin/maintainer.xml") == 1 ]]
strings "$work/root/system/bin/uke-recoveryctl" | rg 'ORIGINAL_USERDATA_ONLY' >/dev/null
strings "$work/root/system/bin/recovery" | rg 'ure_layout_graph' >/dev/null
for page in ure_filesystems ure_linux ure_btrfs ure_partition_journals ure_partition_recovery ure_stock_job ure_stock_job_sources ure_stock_job_payloads ure_stock_job_slots ure_stock_job_journals ure_stock_job_review ure_stock_job_recovery ure_boot_manager ure_boot_review ure_boot_journal; do
    [[ $(xmllint --xpath "count(/recovery/pages/page[@name='$page'])" "$work/root/sbin/maintainer.xml") == 1 ]]
done
for marker in ure-linux-boot-audit filesystem.manage btrfs.manage linux.rescue partition.apply-layout stock.restore-images ure-stock-application ure-image-range-sha256-tree-v1 ure-uefi-one-shot boot-backend-unverified boot-plan-replayed; do
    strings "$work/root/system/bin/uke-recoveryctl" | rg -F "$marker" >/dev/null
done
strings "$work/root/system/bin/recovery" | rg -F 'boot-route-stage-fixture' >/dev/null
bash "$component/tests/check-write-gate.sh"
bash "$component/tests/check-recovery-startup.sh"
cmp "$component/src/device/xiaomi/uke/recovery/root/init.recovery.qcom.rc" "$work/root/init.recovery.qcom.rc"
[[ ! -e $work/root/init.recovery.usb.rc ]]
strings "$work/root/system/bin/init" | rg -F charger_partition >/dev/null
strings "$work/root/system/bin/init" | rg -F ufs_ffu >/dev/null
cmp "$tree/bootable/recovery/gui/theme/common/languages/en.xml" "$work/root/twres/languages/en.xml"
[[ $(xmllint --xpath 'count(/language/resources/string[@name="ure_device_write_blocked"])' "$work/root/twres/languages/en.xml") == 1 ]]
for binary in recovery fastbootd uke-recovery-install; do
    strings "$work/root/system/bin/$binary" | rg -F 'ure-legacy-write-unavailable' >/dev/null
    cmp "$tree/out-public/target/product/uke/recovery/root/system/bin/$binary" "$work/root/system/bin/$binary"
done
for relative in system/lib64/libbootloader_message.so system/bin/init \
    system/bin/android.hardware.boot@1.0-service system/bin/android.hardware.boot@1.1-service \
    system/bin/android.hardware.boot@1.2-service; do
    strings "$work/root/$relative" | rg -F 'ure-legacy-write-unavailable' >/dev/null
    cmp "$tree/out-public/target/product/uke/recovery/root/$relative" "$work/root/$relative"
done
strings "$work/root/system/bin/recovery" | rg -F 'URE_STORAGE_WRITE_BLOCKED' >/dev/null
cmp "$tree/out-public/target/product/uke/system/lib64/libminuitwrp.so" "$work/root/system/lib64/libminuitwrp.so"
for symbol in gr_ttf_setLocale gr_ttf_inspectLayout hb_shape_full fribidi_reorder_line; do
    readelf --dyn-syms --wide "$work/root/system/lib64/libminuitwrp.so" | grep -F "$symbol" > /dev/null
done
bash "$component/src/device/xiaomi/uke/prepare-text-notices.sh" "$tree" "$work/expected-text-notices"
for notice in harfbuzz.txt harfbuzz-MS-USE.txt harfbuzz-source-notices.txt fribidi-LGPL-2.1.txt fribidi-source-notices.txt unicode-data.txt; do
    cmp "$work/expected-text-notices/$notice" "$work/root/system/etc/ure/licenses/$notice"
done
for binary in uke-recoveryctl uke-recovery-install; do
    strings "$work/root/system/bin/$binary" | rg -F '9e55ff8afdf178e424187f0dc7d6dd2fa570308e22d8df7ac895d65017dbc0d7' >/dev/null
done
cmp "$tree/out-public/target/product/uke/system/lib64/libzstd.so" "$work/root/system/lib64/libzstd.so"
for symbol in gr_external_select gr_external_configure gr_external_update ure_mirror_select; do
    readelf --dyn-syms --wide "$work/root/system/lib64/libminuitwrp.so" | grep -F "$symbol" > /dev/null
done
cmp "$component/src/device/xiaomi/uke/ure-tools.lock.json" "$work/root/system/etc/ure/tools.lock.json"
cmp "$tree/out-public/target/product/uke/system/etc/mke2fs.conf" "$work/root/system/etc/mke2fs.conf"
grep -q 'page">ure_home<' "$work/root/twres/pages/advanced.xml"
[[ $(readlink -- "$work/root/system/bin/dropbearkey") == /system/bin/dropbear ]]
[[ ! -e $work/root/system/bin/keystore_cli_v2 ]]
for tool in uke-recoveryctl uke-recovery-install recovery fastbootd dropbear wimlib-imagex ntfsresize fsck.exfat dump.exfat mkfs.exfat fsck.f2fs make_f2fs; do
    cmp "$tree/out-public/target/product/uke/recovery/root/system/bin/$tool" "$work/root/system/bin/$tool"
done
[[ ! -e $work/root/system/bin/uke-btrfs-vm-fixture && $(readlink "$work/root/system/bin/resize.f2fs") == fsck.f2fs ]]
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
    --arg localization "$(sha256sum "$work/localization.json" | cut -d' ' -f1)" \
    --arg ui "$(sha256sum "$work/staged-ui.json" | cut -d' ' -f1)" \
    --arg extracted_ui "$(sha256sum "$work/extracted-ui.json" | cut -d' ' -f1)" \
    --slurpfile ui_parity "$work/ui-comparison.json" \
    --arg ramdisk "$(sha256sum "$work/ramdisk.lz4" | cut -d' ' -f1)" \
    --arg cli "$(sha256sum "$work/root/system/bin/uke-recoveryctl" | cut -d' ' -f1)" \
    --arg runner "$(sha256sum "$component/tests/check-aarch64.sh" | cut -d' ' -f1)" \
    --arg auditor "$(sha256sum "$component/scripts/audit-recovery-image.sh" | cut -d' ' -f1)" \
    --argjson bytes "$bytes" --argjson qemu "$qemu" \
    --slurpfile completion "$work/compile-evidence.json" \
    '{schema_version:1,recovery_image_sha256:$image,localization_inputs_sha256:$localization,shipping_ui_assets_sha256:$ui,
      shipping_ui_identity_basis:"staging inventory; packed modes verified by canonical payload replay",
      extracted_ui_assets_sha256:$extracted_ui,ui_resource_parity:$ui_parity[0],
      build_completion:$completion[0],compressed_ramdisk:{bytes:$bytes,sha256:$ramdisk},native_cli_sha256:$cli,aarch64_runner_sha256:$runner,auditor_sha256:$auditor,validation:{localization_source_inventory:true,extracted_gui_resources_match:true,build_completion_payload_match:true,extracted_ramdisk:true,payload_privacy:true,no_python_payload:true,recursive_zip_scan:true,elf_dependency_closure:true,gui_xml:true,tool_manifest:true,staged_target_binaries_match:true,source_built_layout_renderer_and_pages:true,source_built_mirror_renderer_and_exports:true,source_built_native_management_pages:true,source_built_combined_partition_job:true,source_built_six_lun_stock_job:true,qemu_user_six_lun_stock_job:$qemu,source_built_f2fs_format_and_resize_tools:true,vm_test_binary_excluded:true,qemu_user_fixtures:$qemu,physical_device:false,gui_rendering:false,hardware_rollback:false}}' > "$report"
jq --argjson qemu "$qemu" '.validation.source_built_capacity_adjusted_stock_preflight=true |
    .validation.source_built_one_shot_boot_and_gui=true | .validation.qemu_user_one_shot_boot_fixtures=$qemu |
    .validation.source_built_shared_legacy_write_policy=true |
    .validation.source_built_shared_misc_and_boot_control_write_policy=true |
    .validation.real_efi_variable_write=false | .validation.uke_boot_routing_accepted=false' "$report" > "$work/capacity-preflight-audit.json"
mv -- "$work/capacity-preflight-audit.json" "$report"
echo 'Final compressed ramdisk audit passed; source, emulation and hardware evidence remain separate.'
