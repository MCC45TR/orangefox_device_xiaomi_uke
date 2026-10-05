#!/usr/bin/env bash
# Frozen offline host provenance controls; no provider requests or device writes.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-native}
[[ $mode == native || $mode == sanitizer ]] && [[ $# -le 1 ]]
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" "$mode" bash "$component/tests/check-localization-review.sh" "$mode"
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
record=$(mktemp -d "$component/build/locale-review-evidence-$mode-XXXXXXXX")
bash scripts/native-inputs.sh > "$record/inputs-before.sha256"
cmake -S src/device/xiaomi/uke/recoveryctl -B "$build" "${options[@]}" -DCMAKE_CXX_COMPILER="$compiler" > "$record/configure.log" 2>&1
cmake --build "$build" --target uke-locale-catalog uke-locale-review uke-locale-review-tests uke-locale-key-tests \
    uke-gui-language-tests --parallel 2 > "$record/build.log" 2>&1
ctest --test-dir "$build" --output-on-failure \
    -R '^ure-(localization-key-owner-and-schema|localization-current-source-and-parent-review|gui-language-review-context)$' \
    > "$record/ctest.log" 2>&1
"$build/uke-locale-catalog" prepare "$component" "$record/full" > "$record/prepare.log" 2>&1
"$build/uke-locale-catalog" keys "$record/full/catalog.json" "$record/full/ure-locale-keys.hpp" > "$record/keys.log" 2>&1
"$build/uke-locale-review-tests" "$record" "$build/uke-locale-review" "$component" "$record/full/catalog.json" > "$record/full-review.log" 2>&1
if rg -n 'ERROR: AddressSanitizer|runtime error:|LeakSanitizer' "$record"/{ctest,prepare,keys,full-review}.log; then exit 1; fi
bash scripts/native-inputs.sh > "$record/inputs-after.sha256"
cmp "$record/inputs-before.sha256" "$record/inputs-after.sha256"
bash scripts/native-test-catalog.sh "$build" > "$record/complete-test-catalog.json"
sha256sum "$build"/uke-locale-{catalog,review,review-tests,key-tests} "$build/uke-gui-language-tests" > "$record/binaries.sha256"
jq -n --arg mode "$mode" --arg inputs "$(sha256sum "$record/inputs-before.sha256" | cut -d' ' -f1)" \
    --arg localization "$(sed -n 's/^# localization_source_sha256=//p' "$record/inputs-before.sha256")" \
    --arg compiler "$(sha256sum "$compiler" | cut -d' ' -f1)" \
    --arg catalog_hash "$(sha256sum "$record/full/catalog.json" | cut -d' ' -f1)" \
    --slurpfile catalog "$record/full/catalog.json" --slurpfile tests "$record/complete-test-catalog.json" \
    '{schema_version:1,evidence_scope:"focused-host-offline-translation-provenance",mode:$mode,
      source_inputs_sha256:$inputs,localization_inputs_sha256:$localization,compiler_sha256:$compiler,
      full_catalog_sha256:$catalog_hash,source_row_count:($catalog[0].strings|length),language_count:($catalog[0].languages|length),
      complete_ctest_catalog:$tests[0],executed_ctest_count:3,
      validation:{current_source_context_and_tool_binding:true,complete_parent_child_correspondence:true,
        exact_regional_locale_targets:true,stale_source_and_member_refusals:true,raw_response_byte_binding:true,
        preserved_invalid_publication:true,wrong_meaning_cannot_ship:true,all_non_english_offline_bundles:true,
        actual_gui_language_review_context:true,sanitizers:($mode=="sanitizer"),
        network_requests:false,semantic_review:false,translation_materialization:false,
        complete_native_catalog:false,complete_android_build:false,shipping_gui:false,combined_vm:false,physical_device:false}}' > "$record/verification.json"
cat "$record/ctest.log" "$record/prepare.log" "$record/full-review.log"
printf 'Focused offline translation evidence retained at %s\n' "$record"
