#!/usr/bin/env bash
# Actual complete service TUs and the complete actual client factory function.
# Host mock boundaries only; this does not execute Android service managers.
set -Eeuo pipefail
umask 077
component=${UKE_BOOT_HAL_SOURCE_COMPONENT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "${BASH_SOURCE[0]}" "$@"
fi
[[ $# == 0 ]]
source_tree=${UKE_BOOT_HAL_SOURCE_TREE:-$component/src/upstream/orangefox-android16}
baseline_tree=${UKE_BOOT_HAL_BASELINE_TREE:-}
fixtures=${UKE_BOOT_HAL_TEST_FIXTURES:-$component/tests/ure/boot-hal-admission}
work_root=${UKE_BOOT_HAL_WORK_ROOT:-$component/build/boot-hal-admission-tests}
report=${UKE_BOOT_HAL_REPORT:-$component/reports/private/boot-hal-admission-verification.json}
policy="$component/src/device/xiaomi/uke/recoveryctl/libuke/recovery_write_policy.hpp"
[[ -f $policy && ! -L $policy && -d $fixtures ]]
mkdir -p "$work_root" "$(dirname -- "$report")"
run=$(mktemp -d "$work_root/run-XXXXXXXX")
printf '%s\n' "$run" > "$work_root/latest-run"
trap 'status=$?; if [[ $status != 0 ]]; then printf "Failed host control preserved at %s (status %s).\n" "$run" "$status" >&2; fi' EXIT
compiler=${CXX:-/usr/bin/c++}
compiler_path=$(realpath -- "$(command -v -- "$compiler")")
[[ -f $compiler_path && -x $compiler_path ]]
flags=(-std=c++20 -O0 -Wall -Wextra -Werror -I"$fixtures" -I"$fixtures/mocks" -I"$(dirname -- "$policy")")
inputs() {
    local version file leaf
    sha256sum -- "$policy" "$compiler_path" "${BASH_SOURCE[0]}"
    while IFS= read -r -d '' file; do sha256sum -- "$file"; done < <(find "$fixtures" -type f -print0 | sort -z)
    for version in 1.0 1.1 1.2; do
        for leaf in Android.bp service.cpp; do
            sha256sum -- "$source_tree/hardware/interfaces/boot/$version/default/$leaf"
        done
        sha256sum -- "$source_tree/bootable/recovery/etc/init/android.hardware.boot@$version-service.rc"
        [[ -z $baseline_tree ]] || sha256sum -- "$baseline_tree/hardware/interfaces/boot/$version/default/service.cpp"
    done
    for leaf in Android.bp BootControlClient.cpp; do
        sha256sum -- "$source_tree/hardware/interfaces/boot/aidl/client/$leaf"
    done
    [[ -z $baseline_tree ]] || sha256sum -- "$baseline_tree/hardware/interfaces/boot/aidl/client/BootControlClient.cpp"
    sha256sum -- "$component/src/upstream/orangefox-android16/bootable/recovery/prebuilt/Android.mk"
}
inputs > "$run/inputs.before.sha256"
check_module() {
    local source=$1 kind=$2 module=$3
    awk -v kind="$kind" -v module="$module" '
        $0 == kind " {" {inside=1; named=0; policy=0}
        inside && $0 == "    name: \"" module "\"," {named=1}
        inside && $0 == "    header_libs: [\"libuke-recovery-write-policy-headers\"]," {policy=1}
        inside && /^}$/ {if(named) {found++; if(!policy) invalid=1} inside=0}
        END {if(found!=1 || invalid) exit 1}' "$source"
}
for version in 1.0 1.1 1.2; do
    check_module "$source_tree/hardware/interfaces/boot/$version/default/Android.bp" cc_binary "android.hardware.boot@$version-service"
    rc="$source_tree/bootable/recovery/etc/init/android.hardware.boot@$version-service.rc"
    [[ $(rg -c '^    setenv LD_LIBRARY_PATH ' "$rc") == 1 ]]
    rg -x '    setenv LD_LIBRARY_PATH /system/lib64:/system/lib' "$rc" >/dev/null
    selected='RECOVERY_BINARY_SOURCE_FILES += $(TARGET_OUT_VENDOR_EXECUTABLES)/hw/android.hardware.boot@'"$version"'-service'
    rg -F "$selected" "$component/src/upstream/orangefox-android16/bootable/recovery/prebuilt/Android.mk" >/dev/null
done
check_module "$source_tree/hardware/interfaces/boot/aidl/client/Android.bp" cc_library libboot_control_client
remove_guard() {
    local source=$1 destination=$2 expected_lines=${3:-5}
    [[ $(rg -c '^    const auto decision = ure::legacy_write_decision' "$source") == 1 ]]
    awk '/^    const auto decision = ure::legacy_write_decision/ {drop=1; next}
        drop {if($0 ~ /^    }$/) drop=0; next}
        {print} END {if(drop) exit 1}' "$source" > "$destination"
    [[ $(( $(wc -l < "$source") - $(wc -l < "$destination") )) == "$expected_lines" ]]
}
compile() { timeout 45 "$compiler" "${flags[@]}" "$@"; }
compile -c "$fixtures/service_test.cpp" -o "$run/service-test.o"
compile -c "$fixtures/client_factory_test.cpp" -o "$run/client-test.o"
service_binary() {
    local source=$1 prefix=$2
    compile -Dmain=ure_boot_service_main -c "$source" -o "$prefix.o"
    timeout 30 "$compiler" "$run/service-test.o" "$prefix.o" -o "$prefix"
}
rejected=0
reject() {
    local binary=$1 log=$2 expected=$3 status
    if timeout 10 "$binary" refusal > "$log" 2>&1; then
        printf 'Removed-guard mutant unexpectedly passed: %s\n' "$binary" >&2; exit 1
    else status=$?; fi
    [[ $status == 1 ]]
    rg -F "$expected" "$log" >/dev/null
    rejected=$((rejected+1))
    printf 'PASS removed-guard mutant rejected: %s\n' "$(basename -- "$binary")"
}
for version in 1.0 1.1 1.2; do
    source="$source_tree/hardware/interfaces/boot/$version/default/service.cpp"
    service_binary "$source" "$run/service-$version-guarded"
    timeout 10 "$run/service-$version-guarded" refusal > "$run/service-$version-guarded.log" 2>&1
    rg -x 'ure-legacy-write-unavailable' "$run/service-$version-guarded.log" >/dev/null
    cat "$run/service-$version-guarded.log"
    remove_guard "$source" "$run/service-$version-no-guard.cpp"
    service_binary "$run/service-$version-no-guard.cpp" "$run/service-$version-mutant"
    reject "$run/service-$version-mutant" "$run/service-$version-mutant.log" 'Boot service reached passthrough resolution'
    baseline_source="$run/service-$version-no-guard.cpp"
    [[ -z $baseline_tree ]] || baseline_source="$baseline_tree/hardware/interfaces/boot/$version/default/service.cpp"
    service_binary "$baseline_source" "$run/service-$version-baseline"
    timeout 10 "$run/service-$version-baseline" baseline | tee "$run/service-$version-baseline.log"
done
extract_factory() {
    local source=$1 destination=$2
    {
        printf '#include "factory_harness.hpp"\nnamespace android::hal {\n'
        awk '/^std::unique_ptr<BootControlClient> BootControlClient::WaitForService\(\) \{$/ {active=1; found++}
            active {print}
            active && /^}$/ {active=0; completed++}
            END {if(found!=1 || completed!=1 || active) exit 1}' "$source"
        printf '}  // namespace android::hal\n'
    } > "$destination"
}
factory_binary() {
    local source=$1 prefix=$2
    extract_factory "$source" "$prefix.cpp"
    compile -c "$prefix.cpp" -o "$prefix.o"
    timeout 30 "$compiler" "$run/client-test.o" "$prefix.o" -o "$prefix"
}
client_source="$source_tree/hardware/interfaces/boot/aidl/client/BootControlClient.cpp"
factory_binary "$client_source" "$run/client-guarded"
timeout 10 "$run/client-guarded" refusal > "$run/client-guarded.log" 2> "$run/client-guarded.stderr"
[[ $(wc -l < "$run/client-guarded.stderr") == 4 ]]
[[ $(rg -xc 'ure-legacy-write-unavailable' "$run/client-guarded.stderr") == 4 ]]
cat "$run/client-guarded.log" "$run/client-guarded.stderr"
remove_guard "$client_source" "$run/client-no-guard.cpp" 6
factory_binary "$run/client-no-guard.cpp" "$run/client-mutant"
reject "$run/client-mutant" "$run/client-mutant.log" 'Boot client factory resolved a service'
baseline_source="$run/client-no-guard.cpp"
[[ -z $baseline_tree ]] || baseline_source="$baseline_tree/hardware/interfaces/boot/aidl/client/BootControlClient.cpp"
factory_binary "$baseline_source" "$run/client-baseline"
timeout 10 "$run/client-baseline" baseline | tee "$run/client-baseline.log"
[[ $rejected == 4 ]]
inputs > "$run/inputs.after.sha256"
cmp "$run/inputs.before.sha256" "$run/inputs.after.sha256"
jq -n --arg inputs "$(sha256sum "$run/inputs.before.sha256" | cut -d' ' -f1)" \
    --arg runner "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
    --arg compiler "$(sha256sum "$compiler_path" | cut -d' ' -f1)" \
    --arg date "$(date -u +%F)" \
    --argjson original_baseline "$([[ -n $baseline_tree ]] && printf true || printf false)" \
    '{schema_version:1,evidence_class:"host-boot-hal-admission-boundaries",date:$date,passed:true,
      inputs_sha256:$inputs,runner_sha256:$runner,compiler_sha256:$compiler,
      complete_service_translation_units:3,complete_client_factory_functions:1,complete_client_translation_unit:false,
      guarded_cases:7,unguarded_baseline_cases:7,rejected_guard_removal_mutants:4,
      original_prepatch_baselines:$original_baseline,normal_patched_android_behavior_unchanged:false,
      validation:{actual_shared_policy:true,source_before_after_equal:true,
        service_refuses_before_mocked_passthrough:true,client_refuses_before_mocked_AIDL_and_HIDL_resolution:true,
        unguarded_resolution_counters_reachable:true,client_stderr_code_without_logd:true,source_module_header_wiring:true,
        source_selected_vendor_service_modules:true,source_system_only_service_library_paths:true,
        service_managers_mocked:true,actual_dlopen:false,android_build:false,packed_artifact:false,
        physical_device:false,live_block_access:false,boot_control_functional:false}}' > "$run/verification.json"
cp -- "$run/verification.json" "$report"
printf 'Boot HAL admission host controls passed: 7 refusal cases, 7 unguarded controls, 4 rejected mutants.\n'
