#!/usr/bin/env bash
# JSON command boundaries only; no block or device input is accepted here.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-$component/build/ure-host/uke-recoveryctl}
work=$(mktemp -d "$component/build/profile-cli-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/fake-system"
for profile in global-os3.0.303.0 cn-os3.0.302.0 poco-os2.0.205.0 fixture-accepted; do
    "$binary" storage profile-status --profile "$profile" > "$work/status.json"
    jq -e '.data.current_system_root and (.data.profile_accepted|not) and (.data.live_plan_allowed|not) and
        .data.accepted_live_profile_count==0 and (.data.observations_are_installed_firmware_proof|not) and
        (.data.blockers|index("device-profile-unaccepted"))!=null' "$work/status.json" >/dev/null
done
"$binary" storage profile-status --profile fixture --system-root "$work/fake-system" > "$work/status.json"
jq -e '(.data.current_system_root|not) and (.data.blockers|index("profile-observation-root-untrusted"))!=null' "$work/status.json" >/dev/null
mkfifo "$work/fifo"
reject() {
    local expected=$1; shift
    if timeout 5 "$binary" "$@" > "$work/rejected.json"; then echo 'Profile command unexpectedly accepted an unrelated option' >&2; exit 1; fi
    jq -e --arg code "$expected" '.error.code==$code' "$work/rejected.json" >/dev/null
}
reject invalid-options storage profile-status --profile fixture --image "$work/fifo"
reject invalid-options storage profile-status --profile fixture --confirm unused
reject invalid-options storage profile-status --profile fixture --observation "$work/fifo"
reject invalid-options storage profile-compare-fixture "$work/fifo" --observation "$work/fifo" --object fake
printf '{}\n' > "$work/contract.json"
printf '{}\n' > "$work/observation.json"
reject invalid-profile-contract storage profile-compare-fixture "$work/contract.json" --observation "$work/observation.json"
"$binary" capabilities --system-root "$work/fake-system" > "$work/capabilities.json"
jq -e '.data.device_profile_admission.accepted_live_profile_count==0 and
    (.data.device_profile_admission.live_plan_allowed|not) and .data.device_profile_admission.code=="device-profile-unaccepted"' "$work/capabilities.json" >/dev/null
echo 'Exact device-profile JSON boundaries, synthetic-root refusal, unopened FIFO controls and closed live authority passed; no device access.'
