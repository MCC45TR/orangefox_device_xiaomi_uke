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
bash tests/check-storage-backup.sh
bash tests/check-restore.sh
bash tests/check-stream-restore.sh
bash tests/check-gpt.sh
bash tests/check-stock-gpt.sh
bash tests/check-partition-map.sh
bash tests/check-display.sh
UKE_RECOVERYCTL_BINARY="$component/build/ure-host/uke-recoveryctl" bash tests/check-recoveryctl.sh
bash tests/check-installer.sh
bash tests/check-payload-fixtures.sh
bash scripts/native-inputs.sh > reports/private/native-test-inputs.sha256
jq -n --arg inputs "$(sha256sum reports/private/native-test-inputs.sha256 | cut -d' ' -f1)" \
    --arg binary "$(sha256sum build/ure-host/uke-recoveryctl | cut -d' ' -f1)" \
    '{schema_version:1,native_test_inputs_sha256:$inputs,host_cli_sha256:$binary,validation:{cpp_negative_and_transaction_fixtures:true,cpp_interruption_and_streaming_fixtures:true,cpp_gpt_backup_repair_restore_fixtures:true,cpp_storage_ownership_policy_fixtures:true,cpp_raw_restore_interruption_fixtures:true,cpp_host_stream_restore_fixtures:true,cpp_stock_gpt_reconstruction_and_oem_xml_oracle:true,stock_gpt_cli_fixtures:true,cpp_partition_map_and_bounded_signatures:true,partition_map_cli_fixtures:true,cli_fixtures:true,backup_cli_and_mock_transport_fixtures:true,storage_image_backup_cli_fixtures:true,raw_restore_cli_fixtures:true,host_stream_restore_cli_and_duplex_transport_fixtures:true,gpt_cli_fixtures:true,legacy_identity_fixtures:true,installer_policy_fixtures:true,forbidden_payload_fixtures:true,physical_device:false}}' \
    | jq '.validation.cpp_tablet_display_density_and_actual_renderer_hooks=true | .validation.display_cli_settings_fixtures=true |
        .validation.cpp_external_display_fake_drm_and_edid=true | .validation.cpp_hardware_keyboard_actual_routing=true |
        .validation.cpp_evdev_actual_hotplug=true' \
    > reports/private/native-verification.json
echo 'Native host fixture gates passed and recorded against exact source inputs.'
