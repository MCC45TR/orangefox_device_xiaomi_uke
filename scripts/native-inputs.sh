#!/usr/bin/env bash
# Stable source identities for the native host fixture gate.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$component"
{
    find src/device/xiaomi/uke/recoveryctl tests/ure -type f -print0
    printf '%s\0' scripts/native-inputs.sh tests/run-native.sh tests/check-ure.sh \
        tests/check-recoveryctl.sh tests/check-installer.sh tests/check-payload.sh \
        tests/check-nested-payloads.sh tests/check-payload-fixtures.sh tests/check-backup.sh tests/check-storage-backup.sh tests/check-restore.sh tests/check-gpt.sh scripts/receive-backup.sh
} | sort -z | xargs -0 sha256sum
