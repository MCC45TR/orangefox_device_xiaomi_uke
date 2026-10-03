#!/usr/bin/env bash
# Host-only source, transaction, installer and forbidden-payload fixtures.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$component"
mkdir -p reports/private
cmake -S src/device/xiaomi/uke/recoveryctl -B build/ure-host -G Ninja
cmake --build build/ure-host -j4
ctest --test-dir build/ure-host --output-on-failure
bash tests/check-ure.sh
bash tests/check-backup.sh
bash tests/check-tree-backup.sh
bash tests/check-storage-backup.sh
bash tests/check-restore.sh
bash tests/check-stream-restore.sh
bash tests/check-gpt.sh
bash tests/check-stock-gpt.sh
bash tests/check-stock-job.sh
bash tests/check-partition-map.sh
bash tests/check-layout.sh
bash tests/check-partition-job.sh
bash tests/check-display.sh
bash tests/check-drm-surface.sh
bash tests/check-filesystems.sh
bash tests/check-rescue.sh
bash tests/check-boot-audit.sh
bash tests/check-boot-router.sh
UKE_RECOVERYCTL_BINARY="$component/build/ure-host/uke-recoveryctl" bash tests/check-recoveryctl.sh
bash tests/check-installer.sh
bash tests/check-stock-boot-programming.sh
bash tests/check-payload-fixtures.sh
bash scripts/native-inputs.sh > reports/private/native-test-inputs.sha256
jq -n --arg inputs "$(sha256sum reports/private/native-test-inputs.sha256 | cut -d' ' -f1)" \
    --arg binary "$(sha256sum build/ure-host/uke-recoveryctl | cut -d' ' -f1)" \
    '{schema_version:1,native_test_inputs_sha256:$inputs,host_cli_sha256:$binary,validation:{cpp_negative_and_transaction_fixtures:true,cpp_interruption_and_streaming_fixtures:true,cpp_gpt_backup_repair_restore_fixtures:true,cpp_storage_ownership_policy_fixtures:true,cpp_raw_restore_interruption_fixtures:true,cpp_host_stream_restore_fixtures:true,cpp_stock_gpt_reconstruction_and_oem_xml_oracle:true,stock_gpt_cli_fixtures:true,cpp_partition_map_and_bounded_signatures:true,partition_map_cli_fixtures:true,cli_fixtures:true,backup_cli_and_mock_transport_fixtures:true,storage_image_backup_cli_fixtures:true,raw_restore_cli_fixtures:true,host_stream_restore_cli_and_duplex_transport_fixtures:true,gpt_cli_fixtures:true,legacy_identity_fixtures:true,installer_policy_fixtures:true,forbidden_payload_fixtures:true,physical_device:false}}' \
    | jq '.validation.cpp_tablet_display_density_and_actual_renderer_hooks=true | .validation.display_cli_settings_fixtures=true |
        .validation.cpp_external_display_fake_drm_and_edid=true | .validation.cpp_hardware_keyboard_actual_routing=true |
        .validation.cpp_evdev_actual_hotplug=true | .validation.cpp_linux_home_tree_backup=true | .validation.tree_backup_cli_and_sigkill_resume=true |
        .validation.cpp_userdata_layout_advanced_mode_and_image_rollback=true | .validation.cpp_actual_partition_graph_widget=true | .validation.layout_cli_fixtures=true |
        .validation.cpp_combined_partition_filesystem_job_and_interruption=true | .validation.partition_job_cli=true | .validation.tablet_forced_reboot=false |
        .validation.cpp_six_lun_stock_jobs_and_sigkill=true | .validation.cpp_android_sparse_and_logical_range_oracles=true |
        .validation.cpp_actual_six_lun_stock_gui=true | .validation.stock_job_cli=true | .validation.stock_model_sku_physical_acceptance=false |
        .validation.cpp_capacity_adjusted_boot_programming_pins=true | .validation.installer_full_partition_dtbo_and_corruption=true |
        .validation.independent_stock_boot_programming_catalog=true |
        .validation.cpp_one_shot_boot_and_actual_sigkill=true | .validation.cpp_actual_boot_gui_callbacks=true |
        .validation.boot_router_cli=true | .validation.uefi_variable_fixture_only=true | .validation.uke_boot_routing_accepted=false |
        .validation.cpp_installed_boot_audit_and_operation_policy=true | .validation.cpp_actual_management_callbacks=true |
        .validation.filesystem_staged_tools_and_complete_rollback=true | .validation.distribution_chroot_mount_and_process_cleanup=true |
        .validation.native_boot_asset_codecs=true | .validation.chroot_distro_dispatch_fixture=true | .validation.real_package_database_repair=false |
        .validation.live_block_write=false | .validation.shipping_btrfs_kernel=false' \
    > reports/private/native-verification.json
echo 'Native host fixture gates passed and recorded against exact source inputs.'
