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
bash scripts/prepare-reviewed-patches.sh "$component/src/upstream/orangefox-android16/external/freetype" apply freetype
initial_inputs=$(mktemp reports/private/native-initial-inputs-XXXXXX)
trap 'rm -f -- "$initial_inputs"' EXIT
bash scripts/native-inputs.sh > "$initial_inputs"
cmake -S src/device/xiaomi/uke/recoveryctl -B build/ure-host -G Ninja \
    -DCMAKE_CXX_COMPILER="${CXX:-/usr/bin/c++}" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$component/scripts/host-ccache.sh"
cmake --build build/ure-host -j"$jobs"
bash scripts/native-test-catalog.sh build/ure-host > reports/private/native-test-catalog.json
ctest --test-dir build/ure-host --output-on-failure
bash tests/check-f2fs-metadata-oracle.sh
# Shell fixtures must share one persistent coordinator across their separate
# CLI invocations, just as all GUI/CLI calls do during one recovery session.
operation_scope=$(mktemp -d "$component/build/native-cli-operations-XXXXXX")
export URE_OPERATION_COORDINATOR="$operation_scope/coordinator"
trap 'rm -f -- "$initial_inputs"; if [[ -f $operation_scope/coordinator/owner.json ]]; then printf "Unresolved CLI ownership preserved at %s\n" "$operation_scope" >&2; else rm -rf -- "$operation_scope"; fi' EXIT
bash tests/check-ure.sh
bash tests/check-device-profile.sh
bash tests/check-backup.sh
bash tests/check-tree-backup.sh
bash tests/check-storage-backup.sh
bash tests/check-restore.sh
bash tests/check-stream-restore.sh
bash tests/check-gpt.sh
bash tests/check-stock-gpt.sh
bash tests/check-stock-job.sh
bash tests/check-partition-capabilities.sh
bash tests/check-partition-map.sh
bash tests/check-layout.sh
bash tests/check-partition-job.sh
bash tests/check-display.sh
bash tests/check-menu-rendering.sh
bash scripts/check-ui-icons.sh
bash tests/check-drm-surface.sh
bash tests/check-filesystems.sh
bash tests/check-rescue.sh
bash tests/with-rescue-cgroup.sh "$component/build/ure-host/uke-rescue-resource-stress"
bash tests/check-boot-audit.sh
bash tests/check-boot-router.sh
UKE_RECOVERYCTL_BINARY="$component/build/ure-host/uke-recoveryctl" bash tests/check-recoveryctl.sh
bash tests/check-installer.sh
bash tests/check-write-gate.sh
bash tests/check-boot-hal-admission.sh
bash tests/check-recovery-first-stage-cmdline.sh
bash tests/check-readonly-fstab-import.sh
bash tests/check-fox-command-admission.sh
bash tests/check-vold-key-upgrade.sh
bash tests/check-crypto-service-hardening.sh
bash tests/check-crypto-nullability.sh
bash tests/check-touch-release.sh
bash tests/check-touch-device-state.sh
bash tests/check-telemetry.sh
bash tests/check-fbe-parser.sh
bash tests/check-fbe-gcm.sh
bash tests/check-weaver.sh
bash tests/check-clock.sh
bash tests/check-clock-mount.sh
bash tests/check-native-theme.sh
bash tests/check-ui-preferences.sh
bash tests/check-reviewed-additions.sh
bash tests/check-host-prerequisites.sh
bash tests/check-lp-record-reader.sh
startup_trace_scope=$(mktemp -d "$component/build/native-packed-startup-text-XXXXXXXX")
bash tests/check-packed-startup-trace-oracle.sh "$startup_trace_scope"
bash tests/check-text-patches.sh
bash tests/check-recovery-packaging-hooks.sh
bash tests/check-packed-payload.sh
bash tests/check-ui-resource-parity.sh
bash tests/check-text-layout-resources.sh
bash tests/check-stock-boot-programming.sh
bash tests/check-payload-fixtures.sh
bash tests/check-release-policy.sh
bash tests/check-localization-evidence.sh
bash tests/check-localization-keys.sh native
bash tests/check-gui-language.sh native
bash tests/check-localization-review.sh native
bash tests/check-platform.sh native
bash tests/check-touch-session.sh
[[ ! -f $URE_OPERATION_COORDINATOR/owner.json ]]
cmp <(bash scripts/native-inputs.sh) "$initial_inputs"
cp -- "$initial_inputs" reports/private/native-test-inputs.sha256
jq -n --arg inputs "$(sha256sum reports/private/native-test-inputs.sha256 | cut -d' ' -f1)" \
    --arg localization "$(sed -n 's/^# localization_source_sha256=//p' "$initial_inputs")" \
    --arg binary "$(sha256sum build/ure-host/uke-recoveryctl | cut -d' ' -f1)" \
    --slurpfile catalog reports/private/native-test-catalog.json \
    '{schema_version:1,native_test_inputs_sha256:$inputs,localization_inputs_sha256:$localization,host_cli_sha256:$binary,ctest_test_names:$catalog[0],ctest_executable_count:($catalog[0]|length),validation:{localization_input_closure:true,name_independent_release_policy:true,cpp_negative_and_transaction_fixtures:true,cpp_interruption_and_streaming_fixtures:true,cpp_gpt_backup_repair_restore_fixtures:true,cpp_storage_ownership_policy_fixtures:true,cpp_raw_restore_interruption_fixtures:true,cpp_host_stream_restore_fixtures:true,cpp_stock_gpt_reconstruction_and_oem_xml_oracle:true,stock_gpt_cli_fixtures:true,cpp_partition_map_and_bounded_signatures:true,partition_map_cli_fixtures:true,cli_fixtures:true,backup_cli_and_mock_transport_fixtures:true,storage_image_backup_cli_fixtures:true,raw_restore_cli_fixtures:true,host_stream_restore_cli_and_duplex_transport_fixtures:true,gpt_cli_fixtures:true,legacy_identity_fixtures:true,installer_policy_fixtures:true,forbidden_payload_fixtures:true,physical_device:false}}' \
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
        .validation.host_boot_hal_service_and_client_resolution_refusals=true |
        .validation.host_boot_implementation_text_oracle_controls=true |
        .validation.host_extra_logical_metadata_reader_controls=true |
        .validation.filename_bound_f2fs_metadata_oracle_controls=true |
        .validation.packed_startup_trace_integrity_tested=false |
        .validation.boot_hal_actual_service_managers_tested=false | .validation.boot_hal_packed_artifacts_tested=false |
        .validation.actual_fastbootd_dispatch_and_block_open_refusals=true | .validation.production_installer_refuses_before_open=true |
        .validation.read_only_mounts_preserve_no_replay_options=true' \
    | jq '.validation.production_bounded_unicode_scalars=true | .validation.production_text_raster_parser_and_cache_budgets=true |
        .validation.production_text_allocation_failure_and_rotated_clipping=true | .validation.reviewed_source_stack_unknown_changes_refused=true' \
    | jq '.validation.platform_exact_context_contracts_and_pre_access_refusal=true |
        .validation.platform_read_only_health_and_hold_policy=true |
        .validation.platform_physical_backends_accepted=false' \
    | jq '.validation.host_localization_key_owner_schema_and_exact_header_closure=true |
        .validation.actual_gui_language_review_context=true | .validation.host_offline_translation_current_source_and_parent=true' \
    | jq '.validation.durable_recovery_image_installer_and_initial_publication_sigkill=true |
        .validation.installer_source_independent_resume_and_rollback=true | .validation.readback_only_restore_retries_target_fsync=true |
        .validation.volatile_legacy_installer_removed=true | .validation.physical_installer_durability=false' \
    | jq '.validation.cooperative_common_operation_ownership=true | .validation.production_lifecycle_and_mount_refusals=true |
        .validation.tree_restore_retained_owner_recovery=true | .validation.android_operation_coordinator_accepted=false |
        .validation.physical_ownership_durability=false' \
    | jq '.validation.rescue_aggregate_cgroup_controls=true | .validation.rescue_bounded_allocation_fork_cpu_and_termination=true |
        .validation.rescue_owner_bound_cancel=true | .validation.rescue_admission_refusal_and_retained_cleanup=true |
        .validation.rescue_gui_latency=false | .validation.shipping_rescue_resource_backend_accepted=false |
        .validation.rescue_external_restart_recovery=false' \
    | jq '.validation.per_device_touch_parser_and_release=true | .validation.bounded_telemetry_and_actual_gui_paths=true |
        .validation.bounded_fbe_record_and_kdf_controls=true | .validation.authenticated_fbe_gcm_and_cleanup=true |
        .validation.weaver_config_buffer_reply_and_retry_delay_controls=true |
        .validation.validated_rtc_offset_controls=true | .validation.private_rtc_mount_policy_controls=true | .validation.native_session_theme_controls=true |
        .validation.reviewed_added_source_files=true | .validation.physical_fbe_access=false |
        .validation.persistent_settings_accepted=false | .validation.fresh_image_touch_startup_accepted=false' \
    > reports/private/native-verification.json
echo 'Native host fixture gates passed and recorded against exact source inputs.'
