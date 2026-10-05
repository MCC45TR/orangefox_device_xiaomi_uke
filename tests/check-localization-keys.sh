#!/usr/bin/env bash
# Host-only real catalog/header production; no translation request or device write.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-native}
[[ $mode == native || $mode == sanitizer ]] && [[ $# -le 1 ]]
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" "$mode" bash "$component/tests/check-localization-keys.sh" "$mode"
fi
cd "$component"
export CCACHE_DIR="$component/build/ccache/native"
flags=(-std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Werror -pthread)
options=(-G Ninja -DCMAKE_CXX_COMPILER_LAUNCHER="$component/scripts/host-ccache.sh")
if [[ $mode == sanitizer ]]; then
    compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
    [[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
    build="$component/build/ure-sanitized-clang"
    flags+=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer)
    options+=(-DCMAKE_C_COMPILER="${compiler%++}" -DCMAKE_BUILD_TYPE=Debug
        '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer'
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr')
    export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
    export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
else
    compiler=/usr/bin/c++
    build="$component/build/ure-host"
fi
record=$(mktemp -d "$component/build/locale-key-evidence-$mode-XXXXXXXX")
bash scripts/native-inputs.sh > "$record/inputs-before.sha256"
cmake -S src/device/xiaomi/uke/recoveryctl -B "$build" "${options[@]}" -DCMAKE_CXX_COMPILER="$compiler" > "$record/configure.log" 2>&1
cmake --build "$build" --target uke-locale-catalog uke-locale-key-tests uke-locale-owner-negative \
    uke-display-tests uke-scale-preview-tests uke-layout-gui-tests uke-management-gui-tests uke-partition-job-tests \
    --parallel 2 > "$record/build.log" 2>&1
ctest --test-dir "$build" --output-on-failure \
    -R '^ure-(localization-key-owner-and-schema|tablet-display-density|scale-preview-and-font-ownership|actual-partition-layout-widget|actual-management-callbacks)$' \
    > "$record/ctest.log" 2>&1
if [[ $mode == sanitizer ]]; then
    set +e
    "$build/uke-locale-owner-negative" > "$record/rejected-owner.log" 2>&1
    status=$?
    set -e
    [[ $status -ne 0 ]]
    rg -q 'AddressSanitizer: heap-use-after-free' "$record/rejected-owner.log"
fi
tool="$build/uke-locale-catalog"
mkdir "$record/empty" "$record/one" "$record/unicode" "$record/full"
jq -n '{schema_version:1,strings:[]}' > "$record/empty/catalog.json"
jq -n '{schema_version:1,strings:[{source:"Confirm",name:"ure_confirm"}]}' > "$record/one/catalog.json"
jq -n '{schema_version:1,strings:[{source:"Line \r\n\t \"\\?9 😀 漢 أ",name:"ure_utf8"}]}' > "$record/unicode/catalog.json"
"$tool" prepare "$component" "$record/full" > "$record/prepare.log" 2>&1
for command in jobs split import combine materialize; do
    if "$tool" "$command" "$record/full/catalog.json" "$record/refused-translation-output" \
        > "$record/refused-$command.log" 2>&1; then exit 1; fi
    rg -q 'disabled pending provenance validation' "$record/refused-$command.log"
    [[ ! -e $record/refused-translation-output ]]
done
for case_name in empty one unicode full; do
    fixture="$record/$case_name"
    "$tool" keys "$fixture/catalog.json" "$fixture/ure-locale-keys.hpp" > "$fixture/generate.log" 2>&1
    # Compile the unchanged real consumer against each newly generated header.
    cp -- src/device/xiaomi/uke/ure-localization.hpp "$fixture/ure-localization.hpp"
    "$component/scripts/host-ccache.sh" "$compiler" "${flags[@]}" -I "$fixture" \
        -isystem "$component/src/upstream/orangefox-android16/external/jsoncpp/include" \
        tests/ure/localization_header.cpp "$build/libuke-jsoncpp.a" -o "$fixture/header-check" > "$fixture/compile.log" 2>&1
    "$fixture/header-check" "$fixture/catalog.json" > "$fixture/check.log" 2>&1
    "$tool" keys "$fixture/catalog.json" "$fixture/repeated.hpp"
    cmp "$fixture/ure-locale-keys.hpp" "$fixture/repeated.hpp"
done
if rg -n 'ERROR: AddressSanitizer|runtime error:|LeakSanitizer' "$record/ctest.log" "$record"/{empty,one,unicode,full}/{generate,compile,check}.log; then exit 1; fi
bash scripts/native-inputs.sh > "$record/inputs-after.sha256"
cmp "$record/inputs-before.sha256" "$record/inputs-after.sha256"
sha256sum "$build/uke-locale-catalog" "$build/uke-locale-key-tests" "$build/uke-locale-owner-negative" > "$record/binaries.sha256"
sha256sum "$record"/{empty,one,unicode,full}/ure-locale-keys.hpp > "$record/headers.sha256"
jq -n --arg mode "$mode" --arg inputs "$(sha256sum "$record/inputs-before.sha256" | cut -d' ' -f1)" \
    --arg localization "$(sed -n 's/^# localization_source_sha256=//p' "$record/inputs-before.sha256")" \
    --arg compiler "$(sha256sum "$compiler" | cut -d' ' -f1)" \
    --arg catalog "$(sha256sum "$record/full/catalog.json" | cut -d' ' -f1)" \
    --arg headers "$(sha256sum "$record/headers.sha256" | cut -d' ' -f1)" \
    --arg consumer "$(sha256sum src/device/xiaomi/uke/ure-localization.hpp | cut -d' ' -f1)" \
    --slurpfile full "$record/full/catalog.json" \
    '{schema_version:1,evidence_scope:"focused-host-localization-key-generator",mode:$mode,
      source_inputs_sha256:$inputs,localization_inputs_sha256:$localization,compiler_sha256:$compiler,
      full_catalog_sha256:$catalog,full_catalog_rows:($full[0].strings|length),language_count:($full[0].languages|length),
      header_index_sha256:$headers,actual_lookup_consumer_sha256:$consumer,
      validation:{owner_schema_and_publication:true,empty_one_non_bmp_and_full_header_compilation:true,
        exact_count_order_and_lookup:true,actual_host_callback_and_widget_controls:true,
        asan_original_owner_negative:($mode=="sanitizer"),
        complete_native_catalog:false,translation_provenance:false,semantic_review:false,
        complete_android_build:false,shipping_gui:false,combined_vm:false,physical_device:false}}' > "$record/verification.json"
cat "$record/prepare.log" "$record/ctest.log" "$record"/{empty,one,unicode,full}/check.log
printf 'Focused key evidence retained at %s\n' "$record"
