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
bash tests/check-gpt.sh
UKE_RECOVERYCTL_BINARY="$component/build/ure-host/uke-recoveryctl" bash tests/check-recoveryctl.sh
bash tests/check-installer.sh
bash tests/check-payload-fixtures.sh
bash scripts/native-inputs.sh > reports/private/native-test-inputs.sha256
jq -n --arg inputs "$(sha256sum reports/private/native-test-inputs.sha256 | cut -d' ' -f1)" \
    --arg binary "$(sha256sum build/ure-host/uke-recoveryctl | cut -d' ' -f1)" \
    '{schema_version:1,native_test_inputs_sha256:$inputs,host_cli_sha256:$binary,validation:{cpp_negative_and_transaction_fixtures:true,cpp_interruption_and_streaming_fixtures:true,cpp_gpt_backup_repair_restore_fixtures:true,cpp_storage_ownership_policy_fixtures:true,cli_fixtures:true,backup_cli_and_mock_transport_fixtures:true,storage_image_backup_cli_fixtures:true,gpt_cli_fixtures:true,legacy_identity_fixtures:true,installer_policy_fixtures:true,forbidden_payload_fixtures:true,physical_device:false}}' \
    > reports/private/native-verification.json
echo 'Native host fixture gates passed and recorded against exact source inputs.'
