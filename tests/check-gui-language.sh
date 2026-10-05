#!/usr/bin/env bash
# Actual host callbacks with private images; no Android window or translator.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-native}
[[ $mode == native || $mode == sanitizer ]] && [[ $# -le 1 ]]
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" "$mode" bash "$component/tests/check-gui-language.sh" "$mode"
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
record=$(mktemp -d "$component/build/gui-language-evidence-$mode-XXXXXXXX")
bash scripts/native-inputs.sh > "$record/inputs-before.sha256"
cmake -S src/device/xiaomi/uke/recoveryctl -B "$build" "${options[@]}" -DCMAKE_CXX_COMPILER="$compiler" > "$record/configure.log" 2>&1
cmake --build "$build" --target uke-gui-language-tests uke-management-gui-tests \
    uke-gui-backend-control-tests uke-gui-btrfs-control-tests uke-partition-job-tests \
    --parallel 2 > "$record/build.log" 2>&1
ctest --test-dir "$build" --output-on-failure \
    -R '^ure-(actual-gui-backend-control|actual-gui-btrfs-control|actual-management-callbacks|gui-language-review-context)$' \
    > "$record/ctest.log" 2>&1
if rg -n 'ERROR: AddressSanitizer|runtime error:|LeakSanitizer' "$record/ctest.log"; then exit 1; fi
bash scripts/native-inputs.sh > "$record/inputs-after.sha256"
cmp "$record/inputs-before.sha256" "$record/inputs-after.sha256"
bash scripts/native-test-catalog.sh "$build" > "$record/complete-test-catalog.json"
sha256sum "$build"/uke-{gui-language,management-gui,gui-backend-control,gui-btrfs-control,partition-job}-tests > "$record/binaries.sha256"
jq -n --arg mode "$mode" --arg inputs "$(sha256sum "$record/inputs-before.sha256" | cut -d' ' -f1)" \
    --arg localization "$(sed -n 's/^# localization_source_sha256=//p' "$record/inputs-before.sha256")" \
    --arg compiler "$(sha256sum "$compiler" | cut -d' ' -f1)" \
    --arg gui "$(sha256sum src/device/xiaomi/uke/ure-gui.cpp | cut -d' ' -f1)" \
    --arg generator "$(sha256sum tests/generate-management-hooks.sh | cut -d' ' -f1)" \
    --slurpfile catalog "$record/complete-test-catalog.json" \
    '{schema_version:1,evidence_scope:"focused-host-gui-language-review",mode:$mode,
      source_inputs_sha256:$inputs,localization_inputs_sha256:$localization,compiler_sha256:$compiler,
      actual_gui_source_sha256:$gui,actual_hook_generator_sha256:$generator,
      complete_ctest_catalog:$catalog[0],executed_ctest_count:4,
      validation:{stale_locale_completion_discarded:true,regional_language_review_required:true,
        fresh_review_and_native_backup_verified:true,filesystem_and_exact_btrfs_controls:true,
        worker_gui_state_isolation:true,sanitizers:($mode=="sanitizer"),
        complete_native_catalog:false,translation_provenance:false,semantic_review:false,
        complete_android_build:false,shipping_gui:false,combined_vm:false,physical_device:false}}' > "$record/verification.json"
cat "$record/ctest.log"
printf 'Focused language evidence retained at %s\n' "$record"
