#!/usr/bin/env bash
# Extract and audit the actual header-v4 LZ4 ramdisk, never only staging files.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
image=$(realpath -- "${1:?Usage: audit-recovery-image.sh IMAGE REPORT_JSON [--qemu|--qemu-startup]}")
report=${2:?Missing output report}
source "$component/scripts/release-policy-lib.sh"
release_output_mutable "$component" "$report"
mode=${3:-}
[[ -z $mode || $mode == --qemu || $mode == --qemu-startup ]]
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
bash "$component/scripts/check-touch-payload.sh" "$work/root" "$work/touch-payload.json"
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
for binary in recovery fastbootd; do
    strings "$work/root/system/bin/$binary" | rg -F 'ure-legacy-write-unavailable' >/dev/null
    cmp "$tree/out-public/target/product/uke/recovery/root/system/bin/$binary" "$work/root/system/bin/$binary"
done
strings "$work/root/system/bin/uke-recovery-install" | rg -F 'installer-durability-unavailable:' >/dev/null
cmp "$tree/out-public/target/product/uke/recovery/root/system/bin/uke-recovery-install" "$work/root/system/bin/uke-recovery-install"
for relative in system/lib64/libbootloader_message.so system/bin/init; do
    strings "$work/root/$relative" | rg -F 'ure-legacy-write-unavailable' >/dev/null
    cmp "$tree/out-public/target/product/uke/recovery/root/$relative" "$work/root/$relative"
done
for version in 1.0 1.1 1.2; do
    binary="android.hardware.boot@$version-service"
    strings "$work/root/system/bin/$binary" | rg -F 'ure-legacy-write-unavailable' >/dev/null
    # This recovery product resolves TARGET_COPY_OUT_VENDOR to system/vendor.
    # OrangeFox copies these actual vendor variants into its system/bin.
    cmp "$tree/out-public/target/product/uke/system/vendor/bin/hw/$binary" "$work/root/system/bin/$binary"
    rc="system/etc/init/$binary.rc"
    cmp "$tree/bootable/recovery/etc/init/$binary.rc" "$work/root/$rc"
    [[ $(rg -c '^    setenv LD_LIBRARY_PATH ' "$work/root/$rc") == 1 ]]
    rg -x '    setenv LD_LIBRARY_PATH /system/lib64:/system/lib' "$work/root/$rc" >/dev/null
done
strings "$work/root/system/lib64/libboot_control_client.so" | rg -F 'ure-legacy-write-unavailable' >/dev/null
cmp "$tree/out-public/target/product/uke/system/lib64/libboot_control_client.so" "$work/root/system/lib64/libboot_control_client.so"
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
startup_qemu=false
startup_runner="$component/tests/check-packed-startup-refusals.sh"
startup_runner_sha256=$(sha256sum "$startup_runner" | cut -d' ' -f1)
startup_oracle_sha256=$(sha256sum "$component/tests/packed-startup-trace-lib.sh" | cut -d' ' -f1)
startup_result_sha256=''
printf 'null\n' > "$work/packed-startup-refusals-summary.json"
if [[ $mode == --qemu || $mode == --qemu-startup ]]; then
    # The sealed build and canonical packed payload were authenticated above.
    # Execute the same extracted ELFs; retain the helper's separate receipt.
    bash "$startup_runner" "$work/root" "$work/packed-startup-refusals.json"
    jq -e --arg runner "$startup_runner_sha256" --arg oracle "$startup_oracle_sha256" '
        .schema_version == 1 and .passed == true and .runner_exit_status == 0 and
        .runner_sha256 == $runner and .boot_lookup_oracle_sha256 == $oracle and
        .expected_cases == 26 and .completed_cases == 26 and
        .unmodified_packed_binaries == true and .runner_and_fixture_inputs_unchanged == true and
        .fixture.regular_image_unchanged == true and .fixture.missing_image_still_absent == true and
        .isolation.host_dev_exposed == false and .isolation.host_sys_exposed == false and
        .isolation.network_namespace_isolated == true and .isolation.trace_and_diagnostics_separate == true and
        .isolation.trace_integrity_against_target_tampering == false and
        .execution_environment.production_boot_service_environment_match == false and
        .execution_environment.ld_library_path == "/payload/system/lib64:/payload/vendor/lib64" and
        .validation.physical_device == false and
        .validation.complete_hal_safety == false and (.cases | type) == "array" and
        (.cases | length) == 26 and all(.cases[];
            .passed == true and .expected_target_exit_in_trace == true and
            .expected_stderr_diagnostic_seen == true and .no_binder_open_attempt == true and
            .no_block_path_open_attempt == true and .no_boot_implementation_lookup_attempt == true and
            .no_vendor_odm_boot_implementation_access == true and
            .no_fixture_input_open_attempt == true)' "$work/packed-startup-refusals.json" >/dev/null
    startup_result_sha256=$(sha256sum "$work/packed-startup-refusals.json" | cut -d' ' -f1)
    # Raw per-case traces and complete receipts remain in the helper's private
    # job. Embed aggregate identities and outcomes without raw diagnostics.
    jq '{schema_version,evidence_class,passed,runner_exit_status,expected_cases,completed_cases,
        runner_sha256,boot_lookup_oracle_sha256,qemu_sha256,payload_provenance,unmodified_packed_binaries,
        runner_and_fixture_inputs_unchanged,elf_manifests,input_manifests,fixture,isolation,
        execution_environment,validation,scope}' "$work/packed-startup-refusals.json" > "$work/packed-startup-refusals-summary.json"
    startup_qemu=true
fi
if [[ $mode == --qemu ]]; then
    # Positive mutation fixtures require their own accepted execution backend.
    # A startup-only run must never claim that broader functional coverage.
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
    --arg startup_runner "$startup_runner_sha256" --arg startup_oracle "$startup_oracle_sha256" --arg startup_result "$startup_result_sha256" \
    --arg audit_mode "${mode:-static}" --argjson startup_qemu "$startup_qemu" \
    --slurpfile startup_refusals "$work/packed-startup-refusals-summary.json" \
    --arg auditor "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
    --argjson bytes "$bytes" --argjson qemu "$qemu" \
    --slurpfile completion "$work/compile-evidence.json" \
    '{schema_version:1,audit_mode:$audit_mode,recovery_image_sha256:$image,localization_inputs_sha256:$localization,shipping_ui_assets_sha256:$ui,
      shipping_ui_identity_basis:"staging inventory; packed modes verified by canonical payload replay",
      extracted_ui_assets_sha256:$extracted_ui,ui_resource_parity:$ui_parity[0],
      packed_startup_refusals:{runner_sha256:$startup_runner,boot_lookup_oracle_sha256:$startup_oracle,executed:$startup_qemu,
        result_receipt_sha256:(if $startup_qemu then $startup_result else null end),result:$startup_refusals[0]},
      build_completion:$completion[0],compressed_ramdisk:{bytes:$bytes,sha256:$ramdisk},native_cli_sha256:$cli,aarch64_runner_sha256:$runner,auditor_sha256:$auditor,validation:{localization_source_inventory:true,extracted_gui_resources_match:true,build_completion_payload_match:true,extracted_ramdisk:true,payload_privacy:true,no_python_payload:true,recursive_zip_scan:true,elf_dependency_closure:true,gui_xml:true,tool_manifest:true,staged_target_binaries_match:true,source_built_layout_renderer_and_pages:true,source_built_mirror_renderer_and_exports:true,source_built_native_management_pages:true,source_built_combined_partition_job:true,source_built_six_lun_stock_job:true,qemu_user_six_lun_stock_job:$qemu,source_built_f2fs_format_and_resize_tools:true,vm_test_binary_excluded:true,qemu_user_fixtures:$qemu,physical_device:false,gui_rendering:false,hardware_rollback:false}}' > "$report"
jq --argjson qemu "$qemu" --argjson startup_qemu "$startup_qemu" '.validation.source_built_capacity_adjusted_stock_preflight=true |
    .validation.source_built_one_shot_boot_and_gui=true | .validation.qemu_user_one_shot_boot_fixtures=$qemu |
    .validation.source_built_shared_legacy_write_policy=true |
    .validation.source_built_shared_misc_and_boot_control_write_policy=true |
    .validation.source_built_installer_durability_refusal=true |
    .validation.source_built_boot_service_pre_resolution_refusal=true |
    .validation.source_built_boot_control_client_pre_resolution_refusal=true |
    .validation.source_system_only_boot_service_library_paths=true |
    .validation.qemu_user_packed_startup_refusals=$startup_qemu |
    .validation.boot_control_functional=false | .validation.external_installed_boot_hal_accepted=false |
    .validation.real_efi_variable_write=false | .validation.uke_boot_routing_accepted=false' "$report" > "$work/capacity-preflight-audit.json"
mv -- "$work/capacity-preflight-audit.json" "$report"
echo 'Final compressed ramdisk audit passed; source, emulation and hardware evidence remain separate.'
