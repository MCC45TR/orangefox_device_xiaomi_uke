#!/usr/bin/env bash
# No block targets, device inventory, credentials, mappings or mounts.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"$binary" partition capabilities --json > "$work/capabilities.json"
jq -e '.data | .format=="ure-partition-capabilities" and .read_only and (.physical_test_record|not) and
    (.regular_image | .target_validation_required and .allocation_pool=="ORIGINAL_USERDATA_ONLY" and
      (.encrypted_userdata_preservation|not) and (.before_userdata_data_migration|not) and .before_userdata_recreate and
      .before_userdata_required_mode=="advanced" and .before_userdata_required_policy=="recreate" and
      .existing_shared_esp_policy=="PRESERVE_EXACT_BYTES" and .new_esp_allocation_to_retain_existing=="zero" and
      (.filesystem_tools|length)==6) and
    (.live_device | (.repartition_available|not) and (.userdata_shrink_available|not) and (.six_lun_stock_restore_available|not) and
      (.advanced_mode_bypasses_admission|not) and (.credential_use_allowed|not) and (.mapper_creation_allowed|not) and (.encrypted_mount_allowed|not) and
      ([.blockers[].code] | length==9 and (unique|length)==9 and index("android-fbe-trust-unverified")!=null and index("forced-restart-durability-unverified")!=null))' "$work/capabilities.json" >/dev/null
expect() {
    local expected=$1
    shift
    if timeout 5 "$binary" "$@" --json > "$work/refused.json"; then
        echo 'Unaccepted partition capability was enabled' >&2
        exit 1
    fi
    jq -e --arg code "$expected" '.error.code==$code' "$work/refused.json" >/dev/null
}
expect invalid-options partition capabilities --profile global-os3.0.303.0
expect invalid-options partition capabilities --confirm unused
# A FIFO would hang if read. The missing system root would fail if opened.
mkfifo "$work/unopened-request"
for operation in job-plan job-execute job-inspect job-resume job-rollback job-cancel; do
    expect live-repartition-unavailable partition "$operation" "$work/unopened-request" --object sysfs:synthetic-unopened \
        --system-root "$work/unopened-system-root"
done
expect invalid-options partition job-plan "$work/unopened-request" --object sysfs:synthetic-unopened --image "$work/unopened-image"
expect invalid-options partition job-plan "$work/unopened-request" --object sysfs:synthetic-unopened --sector-size 4096
expect invalid-options partition job-inspect "$work/unopened-request" --object sysfs:synthetic-unopened --confirm unused
printf '%s\n' 'Partition capability scope, runtime-tool reporting, stable live trust blockers and refusal before request/root/target access passed; no device effects.'
