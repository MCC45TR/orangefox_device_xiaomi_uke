#!/usr/bin/env bash
# Real policy/entry-point refusals. JSON presence fixtures are not build receipts.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "$component/tests/check-release-policy.sh" "$@"
fi
work=$(mktemp -d "$component/build/release-policy-fixture-XXXXXXXX")
policy="$component/configs/release-policy.json"
driver="$component/scripts/release-policy.sh"
mkdir -p "$work/private" "$work/candidate-alpha"
refuse() {
    if "$@" > "$work/refused.log" 2>&1; then echo 'Expected release-policy refusal.' >&2; exit 1; fi
}
for class in experimental vm-reviewed function-reviewed; do
    bash "$driver" plan "$class" > "$work/$class-request.json"
    bash "$driver" normalize "$work/$class-request.json" > "$work/$class-plan.json"
    bash "$driver" validate "$work/$class-plan.json"
done
diff <(jq '.required_receipts' "$work/function-reviewed-plan.json") <(
    printf '["btrfs-vm","build","functional-btrfs","functional-core","functional-filesystems","functional-rescue","gui-vm","image-audit","native","package-repeat","partition-vm","sanitizers","stock-namespace-vm","write-gate-vm"]\n' | jq .
)
for class in experimental vm-reviewed function-reviewed; do
    jq -e '.physical_acceptance==false and .shipping_kernel_acceptance==false and .complete_feature_acceptance==false' "$work/$class-plan.json" >/dev/null
done
refuse bash "$driver" plan device-validated
jq '.capabilities|=reverse' "$work/function-reviewed-request.json" > "$work/reordered.json"
bash "$driver" normalize "$work/reordered.json" > "$work/reordered-plan.json"
cmp "$work/reordered-plan.json" "$work/function-reviewed-plan.json"
for change in '.capabilities|=.[1:]' '.capabilities+=[.capabilities[0]]' '.capabilities+=["unknown-feature"]' '.release_class="unknown"' '.candidate="special-name"'; do
    jq "$change" "$work/function-reviewed-request.json" > "$work/invalid.json"
    refuse bash "$driver" normalize "$work/invalid.json"
done
for change in '.required_receipts=[]' '.policy_sha256="stale"' '.release_class="experimental"' '.physical_acceptance=true'; do
    jq "$change" "$work/function-reviewed-plan.json" > "$work/invalid-plan.json"
    refuse bash "$driver" validate "$work/invalid-plan.json"
done
# These JSON objects only exercise presence/parse preflight. They cannot pass
# describe-prerelease's independent exact build, source and semantic checks.
while IFS=$'\t' read -r id scope file; do
    root="$work/private"
    [[ $scope != artifact ]] || root="$work/candidate-alpha"
    printf '{"policy_presence_fixture":true}\n' > "$root/$file"
done < <(jq -r '.receipts|to_entries[]|[.key,.value.scope,.value.file]|@tsv' "$policy")
cp "$work/function-reviewed-plan.json" "$work/candidate-alpha/RELEASE-POLICY.json"
bash "$driver" preflight "$work/function-reviewed-plan.json" "$work/candidate-alpha" "$work/private"
missing="$work/private/functional-filesystems-vm-verification.json"
mv "$missing" "$work/missing.json"
for name in candidate-alpha renamed-candidate arbitrary-new-release; do
    if [[ $name != candidate-alpha ]]; then mv "$work/$previous" "$work/$name"; fi
    previous=$name
    before=$(cd "$work/$name"; find . -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)
    refuse bash "$driver" preflight "$work/function-reviewed-plan.json" "$work/$name" "$work/private"
    rg -q 'Required release receipt.*functional-filesystems' "$work/refused.log"
    after=$(cd "$work/$name"; find . -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)
    [[ $before == "$after" ]]
done
artifact="$work/arbitrary-new-release"
mv "$work/missing.json" "$missing"
# Exercise every receipt requirement, rather than a single chosen VM file.
while IFS=$'\t' read -r id scope file; do
    root="$work/private"
    [[ $scope != artifact ]] || root="$artifact"
    mv "$root/$file" "$work/missing.json"
    refuse bash "$driver" preflight "$work/function-reviewed-plan.json" "$artifact" "$work/private"
    rg -q "Required release receipt.*$id" "$work/refused.log"
    ln -s "$work/missing.json" "$root/$file"
    refuse bash "$driver" preflight "$work/function-reviewed-plan.json" "$artifact" "$work/private"
    rm "$root/$file"
    mv "$work/missing.json" "$root/$file"
done < <(jq -r '.receipts|to_entries[]|[.key,.value.scope,.value.file]|@tsv' "$policy")
cp "$missing" "$work/valid-presence.json"
printf 'malformed JSON\n' > "$missing"
refuse bash "$driver" preflight "$work/function-reviewed-plan.json" "$artifact" "$work/private"
cp "$work/valid-presence.json" "$missing"
bash "$driver" preflight "$work/function-reviewed-plan.json" "$artifact" "$work/private"
bash "$component/scripts/native-test-catalog.sh" "$component/build/ure-host" > "$work/catalog.json"
printf 'Fixture input manifest, not the real source receipt.\n' > "$work/inputs.sha256"
cp /usr/bin/true "$work/host-cli"
jq -n --arg inputs "$(sha256sum "$work/inputs.sha256" | cut -d' ' -f1)" \
    --arg cli "$(sha256sum "$work/host-cli" | cut -d' ' -f1)" --slurpfile catalog "$work/catalog.json" \
    '{schema_version:1,native_test_inputs_sha256:$inputs,host_cli_sha256:$cli,ctest_test_names:$catalog[0],
      ctest_executable_count:($catalog[0]|length),validation:{physical_device:false,name_independent_release_policy:true}}' > "$work/native.json"
jq 'del(.host_cli_sha256)|.compiler_sha256="55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f"|
    .validation={physical_device:false,address_sanitizer:true,undefined_behavior_sanitizer:true,leak_detection:true,halt_on_error:true}' \
    "$work/native.json" > "$work/sanitizers.json"
pair() { bash "$driver" native-pair "$work/native.json" "$1" "$work/inputs.sha256" "$work/catalog.json" "$work/host-cli"; }
pair "$work/sanitizers.json"
for change in '.ctest_executable_count=24' '.ctest_test_names[0]="foreign-test"' '.native_test_inputs_sha256="old-input"' \
    '.compiler_sha256="other-compiler"' '.validation.address_sanitizer=false' '.validation.leak_detection="true"' '.validation.physical_device=true'; do
    jq "$change" "$work/sanitizers.json" > "$work/invalid-sanitizers.json"
    refuse pair "$work/invalid-sanitizers.json"
done
printf 'Changed host CLI\n' >> "$work/host-cli"
refuse pair "$work/sanitizers.json"
# Unmodified copies of the production entry points permit an isolated fixture
# component without requiring historical downloads in every source checkout.
fixture_component="$work/component"
mkdir -p "$fixture_component/scripts" "$fixture_component/artifacts/sealed-fixture"
cp "$component/scripts/release-policy-lib.sh" "$fixture_component/scripts/"
printf '{"historical_seal_fixture":true}\n' > "$fixture_component/artifacts/sealed-fixture/ARTIFACT-MANIFEST.json"
printf 'Original fixture bytes\n' > "$fixture_component/artifacts/sealed-fixture/original"
for script in package-prerelease describe-prerelease archive-release-sources check-package-repeat; do
    cp "$component/scripts/$script.sh" "$fixture_component/scripts/"
    cmp "$component/scripts/$script.sh" "$fixture_component/scripts/$script.sh"
    refuse bash "$fixture_component/scripts/$script.sh" sealed-fixture
    rg -q 'sealed candidate is immutable' "$work/refused.log"
done
[[ $(cat "$fixture_component/artifacts/sealed-fixture/original") == 'Original fixture bytes' ]]
for script in audit-recovery-image build-evidence build-evidence-lib; do
    cp "$component/scripts/$script.sh" "$fixture_component/scripts/"
    cmp "$component/scripts/$script.sh" "$fixture_component/scripts/$script.sh"
done
refuse bash "$fixture_component/scripts/audit-recovery-image.sh" "$fixture_component/artifacts/sealed-fixture/original" \
    "$fixture_component/artifacts/sealed-fixture/EXTRACTED-RAMDISK-AUDIT.json"
rg -q 'sealed candidate is immutable' "$work/refused.log"
refuse bash "$fixture_component/scripts/build-evidence.sh" export "$fixture_component/artifacts/sealed-fixture/BUILD-COMPLETION.json"
rg -q 'sealed candidate is immutable' "$work/refused.log"
mkdir -p "$fixture_component/build" "$fixture_component/configs" "$fixture_component/artifacts/policy-bound"
cp "$component/configs/release-policy.json" "$fixture_component/configs/"
cp "$component/scripts/release-policy.sh" "$fixture_component/scripts/"
cp "$work/function-reviewed-plan.json" "$fixture_component/artifacts/policy-bound/RELEASE-POLICY.json"
refuse bash "$fixture_component/scripts/package-prerelease.sh" policy-bound "$work/experimental-request.json"
rg -q 'Existing candidate is bound to a different release policy' "$work/refused.log"
cmp "$work/function-reviewed-plan.json" "$fixture_component/artifacts/policy-bound/RELEASE-POLICY.json"
if [[ -f $component/artifacts/prerelease/ARTIFACT-MANIFEST.json ]]; then
    # Independently hash every historical release file when available locally.
    hash_alpha() { (cd "$component/artifacts/prerelease"; find . -type f -print0 | sort -z | xargs -0 sha256sum) | sha256sum | cut -d' ' -f1; }
    before=$(hash_alpha)
    for script in package-prerelease describe-prerelease archive-release-sources check-package-repeat; do
        refuse bash "$component/scripts/$script.sh" prerelease
        rg -q 'sealed candidate is immutable' "$work/refused.log"
    done
    refuse bash "$component/scripts/audit-recovery-image.sh" "$component/artifacts/prerelease/OrangeFox-uke-recovery.img" \
        "$component/artifacts/prerelease/EXTRACTED-RAMDISK-AUDIT.json"
    rg -q 'sealed candidate is immutable' "$work/refused.log"
    refuse bash "$component/scripts/build-evidence.sh" export "$component/artifacts/prerelease/BUILD-COMPLETION.json"
    rg -q 'sealed candidate is immutable' "$work/refused.log"
    [[ $before == "$(hash_alpha)" ]]
    printf '%s\n' 'All six production entry points preserved the locally available historical sealed alpha byte for byte.'
fi
printf '%s\n' 'Explicit classes/capabilities, renamed-candidate missing receipts, all 14 receipt, catalog/source/CLI/compiler and indirect/invalid/tampered-policy refusals passed; JSON presence and metadata matching are not Android build acceptance.'
