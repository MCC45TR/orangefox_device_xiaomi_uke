#!/usr/bin/env bash
# Offline artifact/header and staged payload inventory. Does not run target code.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$component/src/upstream/orangefox-android16/out-public"
product="$out/target/product/uke"
payload="$product/recovery/root"
candidate=${1:-prerelease}
[[ $candidate =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$ ]]
vm_review_candidate=false
if [[ $candidate == ure-vm-review-alpha || $candidate == ure-function-vm-alpha ]]; then vm_review_candidate=true; fi
destination="$component/artifacts/$candidate"
recovery="$destination/OrangeFox-uke-recovery.img"
temporary="$destination/OrangeFox-uke-fastboot-boot.img"
repeat_record="$component/reports/private/$candidate-first-package.sha256"
if [[ $candidate == prerelease ]]; then repeat_record="$component/reports/private/first-package.sha256"; fi
[[ -s $repeat_record ]] || {
    echo 'Run and record the package-repeat check before sealing the manifest' >&2; exit 1;
}
(cd -- "$destination" && sha256sum -c "$repeat_record" >/dev/null)
[[ -s $destination/EXTRACTED-RAMDISK-AUDIT.json && -s $component/reports/private/native-verification.json ]]
# This regression gate applies to every newly sealed candidate, independent of
# its name. Historical manifests and binaries remain immutable.
jq -e '.validation.shared_legacy_write_gate and .validation.actual_format_data_refuses_before_side_effects and
    .validation.actual_fastbootd_dispatch_and_block_open_refusals and .validation.production_installer_refuses_before_open and
    .validation.read_only_mounts_preserve_no_replay_options' "$component/reports/private/native-verification.json" >/dev/null
jq -e '.validation.source_built_shared_legacy_write_policy' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
jq -e '.validation.cpp_storage_ownership_policy_fixtures and .validation.storage_image_backup_cli_fixtures and .validation.cpp_raw_restore_interruption_fixtures and .validation.raw_restore_cli_fixtures and .validation.cpp_host_stream_restore_fixtures and .validation.host_stream_restore_cli_and_duplex_transport_fixtures and .validation.cpp_stock_gpt_reconstruction_and_oem_xml_oracle and .validation.stock_gpt_cli_fixtures and .validation.cpp_partition_map_and_bounded_signatures and .validation.partition_map_cli_fixtures and (.validation.physical_device==false)' \
    "$component/reports/private/native-verification.json" >/dev/null
jq -e '.validation.cpp_tablet_display_density_and_actual_renderer_hooks and .validation.display_cli_settings_fixtures' \
    "$component/reports/private/native-verification.json" >/dev/null
jq -e '.validation.cpp_external_display_fake_drm_and_edid and .validation.cpp_hardware_keyboard_actual_routing and .validation.cpp_evdev_actual_hotplug' \
    "$component/reports/private/native-verification.json" >/dev/null
jq -e '.validation.cpp_linux_home_tree_backup and .validation.tree_backup_cli_and_sigkill_resume' \
    "$component/reports/private/native-verification.json" >/dev/null
jq -e '.validation.cpp_userdata_layout_advanced_mode_and_image_rollback and .validation.cpp_actual_partition_graph_widget and .validation.layout_cli_fixtures' \
    "$component/reports/private/native-verification.json" >/dev/null
jq -e '.validation.cpp_installed_boot_audit_and_operation_policy and .validation.cpp_actual_management_callbacks and .validation.filesystem_staged_tools_and_complete_rollback and .validation.distribution_chroot_mount_and_process_cleanup and .validation.native_boot_asset_codecs and (.validation.live_block_write==false) and (.validation.real_package_database_repair==false)' \
    "$component/reports/private/native-verification.json" >/dev/null
jq -e --arg image "$(sha256sum "$recovery" | cut -d' ' -f1)" '.recovery_image_sha256==$image and .validation.extracted_ramdisk and .validation.no_python_payload and .validation.elf_dependency_closure and .validation.qemu_user_fixtures' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
jq -e '.validation.source_built_mirror_renderer_and_exports' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
jq -e '.validation.source_built_layout_renderer_and_pages' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
jq -e '.validation.source_built_native_management_pages and .validation.source_built_f2fs_format_and_resize_tools and .validation.vm_test_binary_excluded' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
jq -e --arg runner "$(sha256sum "$component/tests/check-aarch64.sh" | cut -d' ' -f1)" --arg auditor "$(sha256sum "$component/scripts/audit-recovery-image.sh" | cut -d' ' -f1)" '.aarch64_runner_sha256==$runner and .auditor_sha256==$auditor' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
cmp <(bash "$component/scripts/native-inputs.sh") "$component/reports/private/native-test-inputs.sha256"
cp -- "$component/reports/private/native-verification.json" "$destination/NATIVE-HOST-VERIFICATION.json"
cp -- "$component/reports/private/native-test-inputs.sha256" "$destination/NATIVE-TEST-INPUTS.sha256"
vm_record=null
partition_vm_record=null
sanitizer_record=null
gui_vm_record=null
stock_namespace_record=null
functional_vm_records=null
if [[ $candidate == ure-rescue-filesystems-alpha ]]; then
    fixture="$out/soong/.intermediates/device/xiaomi/uke/recoveryctl/uke-btrfs-vm-fixture/android_recovery_arm64_armv8-a/uke-btrfs-vm-fixture"
    jq -e --arg runner "$(sha256sum "$component/tests/check-btrfs-vm.sh" | cut -d' ' -f1)" \
        --arg binary "$(sha256sum "$fixture" | cut -d' ' -f1)" \
        '.passed and .validation_kind=="qemu-system-native-ioctl" and .runner_sha256==$runner and .fixture_elf_sha256==$binary and (.checks|length)>=14 and (.physical_device==false) and (.shipping_kernel_test==false) and (.tablet_hardware_test==false)' \
        "$component/reports/private/btrfs-vm-verification.json" >/dev/null
    cp -- "$component/reports/private/btrfs-vm-verification.json" "$destination/BTRFS-VM-VERIFICATION.json"
    vm_record=$(cat "$destination/BTRFS-VM-VERIFICATION.json")
    cmp "$component/reports/private/sanitizer-test-inputs.sha256" "$component/reports/private/native-test-inputs.sha256"
    jq -e --arg inputs "$(sha256sum "$component/reports/private/native-test-inputs.sha256" | cut -d' ' -f1)" \
        '.native_test_inputs_sha256==$inputs and .ctest_executable_count==17 and .validation.address_sanitizer and .validation.undefined_behavior_sanitizer and .validation.leak_detection and (.validation.physical_device==false)' \
        "$component/reports/private/rescue-sanitizer-verification.json" >/dev/null
    cp -- "$component/reports/private/rescue-sanitizer-verification.json" "$destination/SANITIZER-VERIFICATION.json"
    sanitizer_record=$(cat "$destination/SANITIZER-VERIFICATION.json")
fi
if [[ $candidate == ure-partition-job-alpha ]]; then
    jq -e '.validation.cpp_combined_partition_filesystem_job_and_interruption and .validation.partition_job_cli and (.validation.tablet_forced_reboot==false)' \
        "$component/reports/private/native-verification.json" >/dev/null
    jq -e '.validation.source_built_combined_partition_job' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
    jq -e --arg runner "$(sha256sum "$component/tests/check-partition-job-vm.sh" | cut -d' ' -f1)" \
        --arg binary "$(jq -er .native_cli_sha256 "$destination/EXTRACTED-RAMDISK-AUDIT.json")" \
        '.passed and .validation_kind=="qemu-system-native-partition-job" and .runner_sha256==$runner and .native_cli_sha256==$binary and (.checks|length)>=10 and .interruption=="guest-sysrq-emergency-reboot" and (.physical_device==false) and (.shipping_kernel_test==false) and (.tablet_hardware_test==false) and (.ufs_controller_test==false)' \
        "$component/reports/private/partition-vm-verification.json" >/dev/null
    cp -- "$component/reports/private/partition-vm-verification.json" "$destination/PARTITION-VM-VERIFICATION.json"
    partition_vm_record=$(cat "$destination/PARTITION-VM-VERIFICATION.json")
    cmp "$component/reports/private/partition-sanitizer-inputs.sha256" "$component/reports/private/native-test-inputs.sha256"
    jq -e --arg inputs "$(sha256sum "$component/reports/private/native-test-inputs.sha256" | cut -d' ' -f1)" \
        '.native_test_inputs_sha256==$inputs and .ctest_executable_count==18 and .validation.address_sanitizer and .validation.undefined_behavior_sanitizer and .validation.leak_detection and (.validation.physical_device==false)' \
        "$component/reports/private/partition-sanitizer-verification.json" >/dev/null
    cp -- "$component/reports/private/partition-sanitizer-verification.json" "$destination/SANITIZER-VERIFICATION.json"
    sanitizer_record=$(cat "$destination/SANITIZER-VERIFICATION.json")
fi
if [[ $candidate == ure-stock-job-alpha || $candidate == ure-stock-preflight-alpha || $candidate == ure-boot-router-alpha || $vm_review_candidate == true ]]; then
    expected_tests=21
    if [[ $candidate == ure-boot-router-alpha || $vm_review_candidate == true ]]; then expected_tests=23; fi
    if [[ $vm_review_candidate == true ]]; then expected_tests=24; fi
    jq -e '.validation.cpp_six_lun_stock_jobs_and_sigkill and .validation.cpp_android_sparse_and_logical_range_oracles and .validation.cpp_actual_six_lun_stock_gui and .validation.stock_job_cli and (.validation.stock_model_sku_physical_acceptance==false) and (.validation.tablet_forced_reboot==false)' \
        "$component/reports/private/native-verification.json" >/dev/null
    jq -e '.validation.source_built_six_lun_stock_job and .validation.qemu_user_six_lun_stock_job' \
        "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
    cmp "$component/reports/private/partition-sanitizer-inputs.sha256" "$component/reports/private/native-test-inputs.sha256"
    jq -e --arg inputs "$(sha256sum "$component/reports/private/native-test-inputs.sha256" | cut -d' ' -f1)" --argjson count "$expected_tests" \
        '.native_test_inputs_sha256==$inputs and .ctest_executable_count==$count and .validation.address_sanitizer and .validation.undefined_behavior_sanitizer and .validation.leak_detection and (.validation.physical_device==false)' \
        "$component/reports/private/partition-sanitizer-verification.json" >/dev/null
    cp -- "$component/reports/private/partition-sanitizer-verification.json" "$destination/SANITIZER-VERIFICATION.json"
    cp -- "$component/manifests/stock-payloads-global.json" "$destination/STOCK-PAYLOAD-CATALOG.json"
    cp -- "$component/docs/STOCK-IMAGE-RESTORE.md" "$destination/STOCK-IMAGE-RESTORE.md"
    sanitizer_record=$(cat "$destination/SANITIZER-VERIFICATION.json")
    # The earlier partition candidate's guest-reset record describes a different
    # CLI. Leave both VM records null rather than transferring that evidence.
fi
if [[ $candidate == ure-stock-preflight-alpha || $candidate == ure-boot-router-alpha || $vm_review_candidate == true ]]; then
    jq -e '.validation.cpp_capacity_adjusted_boot_programming_pins and .validation.installer_full_partition_dtbo_and_corruption and .validation.independent_stock_boot_programming_catalog' \
        "$component/reports/private/native-verification.json" >/dev/null
    jq -e '.validation.source_built_capacity_adjusted_stock_preflight' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
    cp -- "$component/manifests/stock-boot-programming-global.json" "$destination/STOCK-BOOT-PROGRAMMING.json"
    cp -- "$component/docs/STOCK-BOOT-PREFLIGHT.md" "$destination/STOCK-BOOT-PREFLIGHT.md"
fi
if [[ $candidate == ure-boot-router-alpha || $vm_review_candidate == true ]]; then
    jq -e '.validation.cpp_one_shot_boot_and_actual_sigkill and .validation.cpp_actual_boot_gui_callbacks and
        .validation.boot_router_cli and .validation.uefi_variable_fixture_only and
        (.validation.uke_boot_routing_accepted==false)' "$component/reports/private/native-verification.json" >/dev/null
    jq -e '.validation.source_built_one_shot_boot_and_gui and .validation.qemu_user_one_shot_boot_fixtures and
        (.validation.real_efi_variable_write==false) and (.validation.uke_boot_routing_accepted==false)' \
        "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
    cp -- "$component/docs/BOOT-ROUTING.md" "$destination/BOOT-ROUTING.md"
fi
if [[ $vm_review_candidate == true ]]; then
    jq -e '.validation.actual_menu_renderer_spacing_and_descriptions and
        .validation.pinned_icon_rgba_verification and .validation.scale_selection_requires_apply and
        .validation.actual_scale_preview_and_font_ownership and .validation.independent_monitor_scale_and_idle_redraw' \
        "$component/reports/private/native-verification.json" >/dev/null
    cp -- "$component/reports/URE-VM-REVIEW.md" "$destination/VM-REVIEW.md"
    cp -- "$component/docs/DISPLAY-SCALING.md" "$component/docs/HOST-BUILD-BUDGET.md" \
        "$component/docs/RECOVERY-INTERFACE.md" "$component/docs/STOCK-VM-FIXTURE.md" "$component/docs/SENSOR-READINESS.md" "$destination/"
    # These fresh records may be attached only to their exact tested ELFs.
    # The graphical VM relinks recovery with adapters and is documented
    # separately; it cannot make the shipping GUI validation flag true.
    fixture="$out/soong/.intermediates/device/xiaomi/uke/recoveryctl/uke-btrfs-vm-fixture/android_recovery_arm64_armv8-a/uke-btrfs-vm-fixture"
    jq -e --arg runner "$(sha256sum "$component/tests/check-btrfs-vm.sh" | cut -d' ' -f1)" \
        --arg binary "$(sha256sum "$fixture" | cut -d' ' -f1)" \
        '.passed and .validation_kind=="qemu-system-native-ioctl" and .runner_sha256==$runner and .fixture_elf_sha256==$binary and (.checks|length)>=14 and (.physical_device==false) and (.shipping_kernel_test==false) and (.tablet_hardware_test==false)' \
        "$component/reports/private/btrfs-vm-verification.json" >/dev/null
    jq -e --arg runner "$(sha256sum "$component/tests/check-partition-job-vm.sh" | cut -d' ' -f1)" \
        --arg binary "$(jq -er .native_cli_sha256 "$destination/EXTRACTED-RAMDISK-AUDIT.json")" \
        '.passed and .validation_kind=="qemu-system-native-partition-job" and .runner_sha256==$runner and .native_cli_sha256==$binary and (.checks|length)>=10 and .interruption=="guest-sysrq-emergency-reboot" and (.physical_device==false) and (.shipping_kernel_test==false) and (.tablet_hardware_test==false) and (.ufs_controller_test==false)' \
        "$component/reports/private/partition-vm-verification.json" >/dev/null
    cp -- "$component/reports/private/btrfs-vm-verification.json" "$destination/BTRFS-VM-VERIFICATION.json"
    cp -- "$component/reports/private/partition-vm-verification.json" "$destination/PARTITION-VM-VERIFICATION.json"
    vm_record=$(cat "$destination/BTRFS-VM-VERIFICATION.json")
    partition_vm_record=$(cat "$destination/PARTITION-VM-VERIFICATION.json")
    jq -e --arg runner "$(sha256sum "$component/tests/check-gui-vm.sh" | cut -d' ' -f1)" \
        --arg controller "$(sha256sum "$component/tests/gui-vm-control.sh" | cut -d' ' -f1)" \
        --arg binary "$(sha256sum "$payload/system/bin/recovery" | cut -d' ' -f1)" \
        --arg adapted "$(sha256sum "$component/build/gui-vm/recovery-vm" | cut -d' ' -f1)" \
        --arg properties "$(sha256sum "$component/build/gui-vm/uke-vm-properties" | cut -d' ' -f1)" \
        --arg trie "$(sha256sum "$component/build/gui-vm/property_info" | cut -d' ' -f1)" \
        '.passed and .validation_kind=="manually-reviewed-adapted-orangefox-gui" and
        .runner_sha256==$runner and .controller_sha256==$controller and .shipping_recovery_sha256==$binary and (.runs|length)>=2 and
        .portrait_reviewed and .landscape_reviewed and (.reviewed_scale_percentages==[50,75,100]) and
        all(.runs[]; .process_smoke_passed and .reviewed_scale_percentages==[50,75,100] and
            .runner_sha256==$runner and .shipping_recovery_sha256==$binary and .adapted_recovery_sha256==$adapted and
            .property_helper_sha256==$properties and .property_trie_sha256==$trie and
            .mouse_navigation and .keyboard_navigation and (.screenshots|length)>=6) and
        (.physical_device==false) and (.shipping_kernel_test==false) and (.unmodified_shipping_gui_test==false) and
        (.complete_feature_acceptance==false) and (.host_block_attachment==false) and (.nic==false)' \
        "$component/reports/private/adapted-gui-visual-verification.json" >/dev/null
    cp -- "$component/reports/private/adapted-gui-visual-verification.json" "$destination/ADAPTED-GUI-VM-VERIFICATION.json"
    gui_vm_record=$(cat "$destination/ADAPTED-GUI-VM-VERIFICATION.json")
    jq -e --arg runner "$(sha256sum "$component/tests/check-stock-namespace-vm.sh" | cut -d' ' -f1)" \
        --arg fixture "$(sha256sum "$component/tests/fixtures/stock-adb-2026-10-03.json" | cut -d' ' -f1)" \
        --arg generator "$(sha256sum "$component/tests/vm/stock-layout.cpp" | cut -d' ' -f1)" \
        --arg binary "$(jq -er .native_cli_sha256 "$destination/EXTRACTED-RAMDISK-AUDIT.json")" \
        --arg library "$(sha256sum "$payload/system/lib64/liblpdump.so" | cut -d' ' -f1)" \
        --arg adapter "$(sha256sum "$component/build/gui-vm/uke-vm-lpdump" | cut -d' ' -f1)" \
        --arg properties "$(sha256sum "$component/build/gui-vm/uke-vm-properties" | cut -d' ' -f1)" \
        '.passed and .validation_kind=="generic-vm-stock-namespace" and .source_record=="STOCK-ADB-20261003-01" and
        .runner_sha256==$runner and .fixture_sha256==$fixture and .generator_sha256==$generator and .native_cli_sha256==$binary and
        .metadata_library_sha256==$library and .vm_entry_adapter_sha256==$adapter and .property_adapter_sha256==$properties and
        .malformed_input_refusals==["incomplete","duplicate","invalid-index","invalid-extent"] and
        .observed_partition_labels==121 and .observed_disk_groups==6 and .active_logical_extents==8 and .read_only_attachments and
        .physical_geometry=="synthetic" and .guid_identity=="synthetic" and (.firmware_contents==false) and
        (.encrypted_userdata==false) and (.ufs_lu_numbers_known==false) and (.shipping_kernel_test==false) and
        (.physical_device==false) and (.stock_restore_acceptance==false) and (.nic==false) and (.host_block_attachment==false)' \
        "$component/reports/private/stock-namespace-vm-verification.json" >/dev/null
    cp -- "$component/reports/private/stock-namespace-vm-verification.json" "$destination/STOCK-NAMESPACE-VM-VERIFICATION.json"
    cp -- "$component/tests/fixtures/stock-adb-2026-10-03.json" "$destination/STOCK-NAMESPACE-FIXTURE.json"
    stock_namespace_record=$(cat "$destination/STOCK-NAMESPACE-VM-VERIFICATION.json")
fi
if [[ $candidate == ure-function-vm-alpha ]]; then
    functional_records=()
    generic_kernel=$(sha256sum "$component/build/gui-vm/kernel/arch/arm64/boot/Image" | cut -d' ' -f1)
    userspace_inputs=$(
        (cd "$payload"; find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum
         find . -type l -printf '%p\t%l\n' | LC_ALL=C sort) | sha256sum | cut -d' ' -f1
    )
    for group in core filesystems rescue btrfs; do
        jq -e --arg group "$group" --arg runner "$(sha256sum "$component/tests/check-functional-vm.sh" | cut -d' ' -f1)" \
            --arg guest "$(sha256sum "$component/tests/vm/functional-init.sh" | cut -d' ' -f1)" \
            --arg kernel "$generic_kernel" \
            --arg userspace "$userspace_inputs" --arg binary "$(jq -er .native_cli_sha256 "$destination/EXTRACTED-RAMDISK-AUDIT.json")" \
            '.passed and .validation_kind=="qemu-system-shipping-cli-functions" and .group==$group and
             .runner_sha256==$runner and .guest_script_sha256==$guest and .kernel_sha256==$kernel and .payload_inputs_sha256==$userspace and .native_cli_sha256==$binary and
             (.checked_json_records >= ({core:70,filesystems:55,rescue:11,btrfs:30}[$group])) and
             ((.checks|length) >= ({core:6,filesystems:16,rescue:4,btrfs:3}[$group])) and
             (.physical_device==false) and (.shipping_kernel_test==false) and (.modified_shipping_cli==false) and
             (.real_distribution_installation==false) and (.host_block_attachment==false) and (.nic==false) and (.complete_feature_acceptance==false)' \
            "$component/reports/private/functional-$group-vm-verification.json" >/dev/null
        cp -- "$component/reports/private/functional-$group-vm-verification.json" "$destination/FUNCTIONAL-${group^^}-VM-VERIFICATION.json"
        functional_records+=("$destination/FUNCTIONAL-${group^^}-VM-VERIFICATION.json")
    done
    generic_modules=$(
        (cd "$component/build/gui-vm/modules/lib/modules/7.2.8/kernel";
         sha256sum lib/crypto/libblake2b.ko lib/raid/xor/xor.ko lib/raid/raid6/raid6_pq.ko lib/zstd/zstd_compress.ko fs/btrfs/btrfs.ko) |
            sha256sum | cut -d' ' -f1
    )
    jq -e --arg modules "$generic_modules" \
        '.generic_module_inputs_sha256==$modules and .generic_module_debug_stripped and
         .module_strip_tool_sha256=="d2a3191ad2228bb60c35e18466615cb53ed7263c2e73134624349f0367cb1f88"' \
        "$destination/FUNCTIONAL-BTRFS-VM-VERIFICATION.json" >/dev/null
    functional_vm_records=$(jq -s . "${functional_records[@]}")
    cp -- "$component/docs/FUNCTIONAL-VM-TESTS.md" "$component/reports/URE-FUNCTION-VM-REVIEW.md" "$destination/"
fi
for archive in STOCK-GKI-SOURCE.tar.gz RECOVERY-UTILITY-SOURCES.tar.gz; do [[ -s $destination/$archive ]]; done
(cd "$component" && find .gitattributes src/device src/installer src/inventory configs patches manifests scripts tests -type f -print0 | sort -z | xargs -0 sha256sum) > "$destination/PROJECT-INPUTS.sha256"
[[ $(stat -c %s "$recovery") == 104857600 && $(stat -c %s "$temporary") == 100663296 ]]
kernel_bytes=$(od -An -tu4 -j8 -N4 "$temporary" | tr -d ' ')
ramdisk_bytes=$(od -An -tu4 -j12 -N4 "$recovery" | tr -d ' ')
[[ $kernel_bytes == 35432960 && $ramdisk_bytes -gt 0 ]]
kernel_hash=$(dd if="$temporary" bs=1M iflag=skip_bytes,count_bytes skip=4096 count="$kernel_bytes" status=none | sha256sum | cut -d' ' -f1)
[[ $kernel_hash == $(jq -r .kernel_sha256 "$component/manifests/stock-kernel-source.json") ]]
offset=$((4096+(kernel_bytes+4095)/4096*4096))
ramdisk_hash=$(dd if="$recovery" bs=1M iflag=skip_bytes,count_bytes skip=4096 count="$ramdisk_bytes" status=none | sha256sum | cut -d' ' -f1)
temporary_ramdisk_hash=$(dd if="$temporary" bs=1M iflag=skip_bytes,count_bytes skip="$offset" count="$ramdisk_bytes" status=none | sha256sum | cut -d' ' -f1)
[[ $ramdisk_hash == "$temporary_ramdisk_hash" ]]
[[ $(unzip -p "$destination/OrangeFox-uke-flashable.zip" recovery.img | sha256sum | cut -d' ' -f1) == $(sha256sum "$recovery" | cut -d' ' -f1) ]]
"$out/host/linux-x86/bin/avbtool" info_image --image "$recovery" > "$destination/RECOVERY-AVB.txt"
"$out/host/linux-x86/bin/avbtool" info_image --image "$temporary" > "$destination/TEMPORARY-BOOT-AVB.txt"
inventory="$destination/PAYLOAD-FILES.tsv"
printf 'path\tkind\tbytes\tsha256-or-target\n' > "$inventory"
while IFS= read -r -d '' file; do
    relative=${file#"$payload"/}
    if [[ -L $file ]]; then
        printf '%s\tsymlink\t-\t%s\n' "$relative" "$(readlink -- "$file")" >> "$inventory"
    else
        printf '%s\tfile\t%s\t%s\n' "$relative" "$(stat -c %s "$file")" "$(sha256sum "$file" | cut -d' ' -f1)" >> "$inventory"
    fi
done < <(find "$payload" \( -type f -o -type l \) -print0 | sort -z)
jq -n --arg commit "$(git -C "$component" rev-parse HEAD)" \
    --arg tree "$(git -C "$component" rev-parse HEAD^{tree})" \
    --argjson changed "$([[ -n $(git -C "$component" status --porcelain) ]] && echo true || echo false)" \
    --arg inputs "$(sha256sum "$destination/PROJECT-INPUTS.sha256" | cut -d' ' -f1)" \
    --slurpfile audit "$destination/EXTRACTED-RAMDISK-AUDIT.json" \
    --slurpfile fixtures "$destination/NATIVE-HOST-VERIFICATION.json" \
    --slurpfile tools "$destination/URE-TOOLS.json" \
    --argjson btrfs_vm "$vm_record" \
    --argjson partition_vm "$partition_vm_record" \
    --argjson sanitizers "$sanitizer_record" \
    --argjson adapted_gui_vm "$gui_vm_record" \
    --argjson stock_namespace_vm "$stock_namespace_record" \
    --argjson functional_vm "$functional_vm_records" \
    --arg mkbootimg "$(sha256sum "$out/host/linux-x86/bin/mkbootimg" | cut -d' ' -f1)" \
    --arg avbtool "$(sha256sum "$out/host/linux-x86/bin/avbtool" | cut -d' ' -f1)" \
    --arg mkbootimg_commit "$(git -C "$component/src/upstream/orangefox-android16/system/tools/mkbootimg" rev-parse HEAD)" \
    --arg avbtool_commit "$(git -C "$component/src/upstream/orangefox-android16/external/avb" rev-parse HEAD)" \
    --arg kernel "$kernel_hash" --arg ramdisk "$ramdisk_hash" --argjson ramdisk_bytes "$ramdisk_bytes" \
    '{schema_version:2,classification:"experimental-native-candidate",device:"uke",model_targets:["POCO Pad X1","Xiaomi Pad 7"],firmware_profile:"global-os3.0.303.0",firmware_version:"OS3.0.303.0.WOZMIXM",project_source:{base_commit:$commit,base_tree:$tree,worktree_changes:$changed,input_manifest:"PROJECT-INPUTS.sha256",input_manifest_sha256:$inputs},stock_kernel_sha256:$kernel,recovery_ramdisk:{bytes:$ramdisk_bytes,sha256:$ramdisk},host_tools:{mkbootimg:{source_commit:$mkbootimg_commit,executable_sha256:$mkbootimg},avbtool:{source_commit:$avbtool_commit,executable_sha256:$avbtool}},tools:$tools[0],ramdisk_audit:$audit[0],host_fixture_record:$fixtures[0],btrfs_vm_record:$btrfs_vm,partition_vm_record:$partition_vm,sanitizer_record:$sanitizers,adapted_gui_vm_record:$adapted_gui_vm,stock_namespace_vm_record:$stock_namespace_vm,functional_vm_records:$functional_vm,validation:{compile:true,header_sections:true,zip_integrity:true,static_installer:true,host_policy_fixtures:true,payload_privacy:true,no_python_payload:true,source_identification:true,package_repeat:true,binary_reproducibility:false,physical_device:false,gui_rendering:false,rollback_rehearsal:false,complete_roadmap:false},signatures:{avb:"NONE",zip:"unsigned",checksum:"SHA256SUMS"},source_snapshots:["STOCK-GKI-SOURCE.tar.gz","RECOVERY-UTILITY-SOURCES.tar.gz"]}' \
    > "$destination/ARTIFACT-MANIFEST.json"
# Hash every generated public release file once, including source snapshots.
(cd -- "$destination" && find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%f\0' | sort -z | xargs -0 sha256sum > SHA256SUMS)
echo 'Header sections, kernel identity, shared ramdisk and ZIP payload agree; release manifest generated.'
