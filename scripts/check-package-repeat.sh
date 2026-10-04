#!/usr/bin/env bash
# Compare two packages from one accepted build; this is not two clean builds.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "${BASH_SOURCE[0]}" "$@"
fi
source "$component/scripts/release-policy-lib.sh"
candidate=${1:?Candidate and explicit release request required}
[[ $candidate =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$ ]]
destination="$component/artifacts/$candidate"
release_mutable_destination "$destination"
[[ $# == 2 ]]
work=$(mktemp -d "$component/build/package-repeat-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
bash "$component/scripts/package-prerelease.sh" "$candidate" "$2"
cp -- "$destination/SHA256SUMS" "$work/first.sha256"
bash "$component/scripts/package-prerelease.sh" "$candidate" "$2"
cmp "$work/first.sha256" "$destination/SHA256SUMS"
(cd -- "$destination"; sha256sum -c "$work/first.sha256" >/dev/null)
cp -- "$work/first.sha256" "$destination/PACKAGE-REPEAT.sha256"
jq -n --arg policy "$(release_digest "$destination/RELEASE-POLICY.json")" \
    --arg receipt "$(jq -er .receipt_index_sha256 "$destination/BUILD-COMPLETION.json")" \
    --arg files "$(release_digest "$destination/PACKAGE-REPEAT.sha256")" \
    '{schema_version:1,evidence_class:"host-package-repeat",passed:true,iterations:2,
      release_policy_sha256:$policy,build_receipt_index_sha256:$receipt,file_manifest_sha256:$files,
      independent_clean_builds:false,binary_reproducibility:false,physical_device:false}' > "$destination/PACKAGE-REPEAT.json"
printf '%s\n' 'Two packages match the same accepted build and policy; no clean-build reproducibility claim.'
