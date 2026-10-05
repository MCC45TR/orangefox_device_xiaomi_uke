#!/usr/bin/env bash
# Private host fixtures only; no block nodes, credentials, HAL writes or reboot.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-native}
[[ $mode == native || $mode == sanitizer ]] && [[ $# -le 1 ]]
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" "$mode" bash "$component/tests/check-platform.sh" "$mode"
fi
cd "$component"
export CCACHE_DIR="$component/build/ccache/native"
options=(-G Ninja -DCMAKE_CXX_COMPILER_LAUNCHER="$component/scripts/host-ccache.sh")
if [[ $mode == sanitizer ]]; then
    compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
    [[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
    build="$component/build/ure-sanitized-clang"
    options+=(-DCMAKE_C_COMPILER="${compiler%++}" -DCMAKE_BUILD_TYPE=Debug
        '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer'
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr')
    export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
    export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
else
    compiler=/usr/bin/c++
    build="$component/build/ure-host"
fi
record=$(mktemp -d "$component/build/platform-evidence-$mode-XXXXXXXX")
work="$record/fixtures"
mkdir -p "$work/root"
bash scripts/native-inputs.sh > "$record/inputs-before.sha256"
cmake -S src/device/xiaomi/uke/recoveryctl -B "$build" "${options[@]}" -DCMAKE_CXX_COMPILER="$compiler" > "$record/configure.log" 2>&1
cmake --build "$build" --target uke-platform-tests uke-platform-android-tests uke-recoveryctl \
    uke-management-gui-tests uke-partition-job-tests uke-management-tests uke-device-profile-tests \
    --parallel 2 > "$record/build.log" 2>&1
ctest --test-dir "$build" --output-on-failure \
    -R '^ure-(platform-feature-contracts|platform-shipping-policy|exact-device-profile-declarations|installed-boot-audit-and-operation-policy|actual-management-callbacks)$' \
    > "$record/ctest.log" 2>&1
if rg -n 'ERROR: AddressSanitizer|runtime error:|LeakSanitizer' "$record/ctest.log"; then exit 1; fi
binary="$build/uke-recoveryctl"
timeout 10 "$binary" platform capabilities --profile fixture-os3 --system-root "$work/root" --json > "$work/capabilities.json"
jq -e '.data | .format=="ure-platform-capabilities" and .read_only and (.live_action_allowed|not) and
    (.credential_use_allowed|not) and (.mapper_creation_allowed|not) and (.encrypted_mount_allowed|not) and
    (.features|length)==11 and (.features|all(.state=="unavailable" and (.live_action_allowed|not))) and .health.decision=="hold"' "$work/capabilities.json" >/dev/null
timeout 10 "$binary" android preflight --profile fixture-os3 --system-root "$work/root" --json > "$work/android.json"
cmp <(jq -S '.data' "$work/capabilities.json") <(jq -S '.data' "$work/android.json")
# Build exact offline declarations from the native requirement inventory.
# Successful comparison still has no physical/backend/credential authority.
jq -n --slurpfile caps "$work/capabilities.json" \
    '{schema:1,format:"ure-platform-contract",context:{profile:"fixture-os3",identity:{commercial_model:"fixture-pad",model_number:"fixture-number",
      device:"fixture-device",product:"fixture-product",hardware_sku:"fixture-sku",vendor_sku:"fixture-vendor",firmware_version:"fixture-firmware",build_fingerprint:"fixture-build"},
      unit_identity_sha256:("1"*64),boot_id_sha256:("2"*64),kernel_sha256:("3"*64)},
      requirements:[$caps[0].data.required_evidence[] | {check:.,artifact_sha256:("4"*64)}]}' > "$work/contract.json"
context_hash=$(jq -cS '.context' "$work/contract.json" | sha256sum | cut -d' ' -f1)
jq --arg context "$context_hash" \
    '{schema:1,format:"ure-platform-observation",context:.context,evidence:[.requirements[] | .+{context_sha256:$context,passed:true}],
      boot_control:{available:true,consistent_reads:true,slot_suffix:"_a",
        "get-number-slots":{available:true,value:"2"},"get-current-slot":{available:true,value:"0"},
        "get-active-boot-slot":{available:true,value:"0"},"get-snapshot-merge-status":{available:true,value:"none"},
        slots:[{slot:0,bootable:{available:true,value:"1"},successful:{available:true,value:"1"}},
          {slot:1,bootable:{available:true,value:"1"},successful:{available:true,value:"1"}}]}}' "$work/contract.json" > "$work/observation.json"
timeout 5 "$binary" platform compare-fixture "$work/contract.json" --observation "$work/observation.json" --json > "$work/comparison.json"
# JsonCpp and jq use different whitespace. The native comparison returns the
# precise context digest required for offline declarations, without authority.
jq -e '.data.fixture_checks_passed|not' "$work/comparison.json" >/dev/null
context_hash=$(jq -er '.data.context_sha256' "$work/comparison.json")
jq --arg context "$context_hash" '.evidence |= map(.context_sha256=$context)' "$work/observation.json" > "$work/bound-observation.json"
timeout 5 "$binary" platform compare-fixture "$work/contract.json" --observation "$work/bound-observation.json" --json > "$work/comparison.json"
jq -e '.data | .fixture_checks_passed and .fixture_declarations_only and (.live_action_allowed|not) and
    (.comparison_is_artifact_verification|not) and (.comparison_is_authentication|not)' "$work/comparison.json" >/dev/null
expect() {
    local expected=$1
    shift
    if timeout 5 "$binary" "$@" --json > "$work/refusal.json"; then
        printf '%s\n' 'Unsafe platform command unexpectedly succeeded' >&2
        exit 1
    fi
    jq -e --arg code "$expected" '.error.code==$code' "$work/refusal.json" >/dev/null
}
expect invalid-options platform capabilities --profile fixture-os3 --confirm unused
expect invalid-options android preflight --profile fixture-os3 --object unopened
expect invalid-platform-feature platform enable
mkfifo "$work/unopened-request"
for action in fbe-open slot-set snapshot-merge super-apply ota-install second-install; do
    expect platform-action-unavailable android "$action" "$work/unopened-request" --system-root "$work/missing-root" \
        --object unopened --journal "$work/unopened-journal" --confirm synthetic
done
[[ ! -e $work/unopened-journal ]]
bash scripts/native-inputs.sh > "$record/inputs-after.sha256"
cmp "$record/inputs-before.sha256" "$record/inputs-after.sha256"
bash scripts/native-test-catalog.sh "$build" > "$record/complete-test-catalog.json"
sha256sum "$build"/uke-{platform,platform-android,management-gui,partition-job,management,device-profile}-tests "$binary" > "$record/binaries.sha256"
jq -n --arg mode "$mode" --arg inputs "$(sha256sum "$record/inputs-before.sha256" | cut -d' ' -f1)" \
    --arg localization "$(sed -n 's/^# localization_source_sha256=//p' "$record/inputs-before.sha256")" \
    --arg compiler "$(sha256sum "$compiler" | cut -d' ' -f1)" \
    --slurpfile catalog "$record/complete-test-catalog.json" \
    '{schema_version:1,evidence_scope:"focused-host-platform-admission",mode:$mode,source_inputs_sha256:$inputs,
      localization_inputs_sha256:$localization,compiler_sha256:$compiler,complete_ctest_catalog:$catalog[0],executed_ctest_count:5,
      validation:{exact_context_bound_feature_contracts:true,slot_snapshot_and_fallback_refusals:true,
        bounded_read_only_health_observations:true,shipping_policy_early_refusal:true,actual_capability_gui_callback:true,
        cli_pre_access_refusal:true,sanitizers:($mode=="sanitizer"),complete_native_catalog:false,
        complete_android_build:false,combined_vm:false,physical_device:false,live_writer:false,keymint_tee_accepted:false,
        uefi_aloha_accepted:false,health_threshold_policy_accepted:false}}' > "$record/verification.json"
cat "$record/ctest.log"
printf 'Focused platform evidence retained at %s\n' "$record"
