#!/usr/bin/env bash
# Host-only source, transaction, installer and forbidden-payload fixtures.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "$component/tests/run-native.sh" "$@"
fi
jobs=${UKE_HOST_JOBS:-2}
[[ $jobs =~ ^([1-9]|1[0-6])$ ]]
cd "$component"
export CCACHE_DIR=${CCACHE_DIR:-"$component/build/ccache/native"}
mkdir -p "$CCACHE_DIR"
mkdir -p reports/private
initial_inputs=$(mktemp reports/private/native-initial-inputs-XXXXXX)
trap 'rm -f -- "$initial_inputs"' EXIT
bash scripts/native-inputs.sh > "$initial_inputs"
cmake -S src/device/xiaomi/uke/recoveryctl -B build/ure-host -G Ninja \
    -DCMAKE_CXX_COMPILER="${CXX:-/usr/bin/c++}" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$component/scripts/host-ccache.sh"
cmake --build build/ure-host -j"$jobs"
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
bash tests/check-menu-rendering.sh
bash scripts/check-ui-icons.sh
bash tests/check-drm-surface.sh
bash tests/check-filesystems.sh
bash tests/check-rescue.sh
bash tests/check-boot-audit.sh
bash tests/check-boot-router.sh
UKE_RECOVERYCTL_BINARY="$component/build/ure-host/uke-recoveryctl" bash tests/check-recoveryctl.sh
bash tests/check-installer.sh
bash tests/check-write-gate.sh
bash tests/check-text-patches.sh
bash tests/check-stock-boot-programming.sh
bash tests/check-payload-fixtures.sh
cmp <(bash scripts/native-inputs.sh) "$initial_inputs"
cp -- "$initial_inputs" reports/private/native-test-inputs.sha256
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
        .validation.live_block_write=false | .validation.shipping_btrfs_kernel=false |
        .validation.actual_menu_renderer_spacing_and_descriptions=true | .validation.pinned_icon_rgba_verification=true |
        .validation.scale_selection_requires_apply=true | .validation.actual_scale_preview_and_font_ownership=true |
        .validation.independent_monitor_scale_and_idle_redraw=true' \
    | jq '.validation.shared_legacy_write_gate=true | .validation.actual_format_data_refuses_before_side_effects=true |
        .validation.actual_fastbootd_dispatch_and_block_open_refusals=true | .validation.production_installer_refuses_before_open=true |
        .validation.read_only_mounts_preserve_no_replay_options=true' \
    | jq '.validation.production_bounded_unicode_scalars=true | .validation.production_text_raster_parser_and_cache_budgets=true |
        .validation.production_text_allocation_failure_and_rotated_clipping=true | .validation.reviewed_source_stack_unknown_changes_refused=true' \
    | jq '.validation.durable_recovery_image_installer_and_initial_publication_sigkill=true |
        .validation.installer_source_independent_resume_and_rollback=true | .validation.readback_only_restore_retries_target_fsync=true |
        .validation.volatile_legacy_installer_removed=true | .validation.physical_installer_durability=false' \
    > reports/private/native-verification.json
echo 'Native host fixture gates passed and recorded against exact source inputs.'
