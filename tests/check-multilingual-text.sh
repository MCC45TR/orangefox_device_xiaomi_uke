#!/usr/bin/env bash
# Focused actual-renderer evidence. This never replaces complete release gates.
set -euo pipefail
umask 077
export LC_ALL=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:?Pass native or sanitizer}
[[ $# == 1 && ( $mode == native || $mode == sanitizer ) ]]
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" "$mode" bash "$component/tests/check-multilingual-text.sh" "$mode"
fi
cd "$component"
bash scripts/prepare-android-text-layout.sh
bash scripts/prepare-reviewed-patches.sh src/upstream/orangefox-android16/external/freetype check freetype
bash scripts/prepare-recovery-patches.sh src/upstream/orangefox-android16/bootable/recovery check
work=$(mktemp -d "$component/build/text-evidence-$mode-XXXXXXXX")
bash scripts/native-inputs.sh > "$work/inputs-before.sha256"
if [[ $mode == sanitizer ]]; then
    directory=build/ure-sanitized-clang
    compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
    [[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
    cmake -S src/device/xiaomi/uke/recoveryctl -B "$directory" -G Ninja \
        -DCMAKE_CXX_COMPILER="$compiler" -DCMAKE_C_COMPILER="${compiler%++}" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_COMPILER_LAUNCHER="$component/scripts/host-ccache.sh" \
        '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer' \
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr'
    export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
    "$compiler" -fsanitize=undefined -fno-sanitize=vptr -O0 tests/ure/text_runtime_negative.cpp -o "$work/undefined-negative"
    if "$work/undefined-negative" > "$work/undefined-negative.log" 2>&1; then
        echo 'The sanitizer runtime did not halt on the known undefined behavior.' >&2; exit 1
    fi
    rg -q 'signed integer overflow' "$work/undefined-negative.log"
else
    directory=build/ure-host
    compiler=/usr/bin/c++
    cmake -S src/device/xiaomi/uke/recoveryctl -B "$directory" -G Ninja \
        -DCMAKE_CXX_COMPILER="$compiler" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS= -DCMAKE_C_FLAGS= -DCMAKE_EXE_LINKER_FLAGS= \
        -DCMAKE_CXX_COMPILER_LAUNCHER="$component/scripts/host-ccache.sh"
fi
cmake --build "$directory" --parallel "${UKE_HOST_JOBS:-2}" --target uke-text-decoder-tests uke-text-renderer-tests uke-text-multilingual-tests
ctest --test-dir "$directory" --output-on-failure -V \
    -R '^ure-production-(bounded-utf8|text-allocation-and-clipping|multilingual-layout)$' > "$work/tests.log" 2>&1
cat "$work/tests.log"
! rg -q 'runtime error:|ERROR: AddressSanitizer|ERROR: LeakSanitizer' "$work/tests.log"
bash scripts/check-font-resources.sh "$directory/text-font-fixture"
bash src/device/xiaomi/uke/prepare-text-notices.sh src/upstream/orangefox-android16 "$work/notices"
bash scripts/native-inputs.sh > "$work/inputs-after.sha256"
cmp "$work/inputs-before.sha256" "$work/inputs-after.sha256"
for executable in uke-text-decoder-tests uke-text-renderer-tests uke-text-multilingual-tests; do
    sha256sum "$directory/$executable" >> "$work/executables.sha256"
done
find "$directory/text-atlases" -maxdepth 1 -name '*.ppm' -type f -exec sha256sum {} + | sort > "$work/atlases.sha256"
[[ $(wc -l < "$work/atlases.sha256") == 11 ]]
jq -n --arg mode "$mode" --arg source "$(sha256sum "$work/inputs-before.sha256" | cut -d' ' -f1)" \
    --arg compiler "$(sha256sum "$compiler" | cut -d' ' -f1)" \
    --arg localization "$(sed -n 's/^# localization_source_sha256=//p' "$work/inputs-before.sha256")" \
    --arg binaries "$(sha256sum "$work/executables.sha256" | cut -d' ' -f1)" \
    --arg log "$(sha256sum "$work/tests.log" | cut -d' ' -f1)" \
    --arg atlases "$(sha256sum "$work/atlases.sha256" | cut -d' ' -f1)" \
    '{schema_version:1,evidence_scope:"focused-production-text-host",mode:$mode,native_test_inputs_sha256:$source,
      localization_inputs_sha256:$localization,compiler_sha256:$compiler,executable_index_sha256:$binaries,
      test_log_sha256:$log,atlas_index_sha256:$atlases,test_names:["ure-production-bounded-utf8",
      "ure-production-text-allocation-and-clipping","ure-production-multilingual-layout"],
      validation:{source_before_after:true,original_font_bytes:true,production_script_controls:true,
      allocation_failure:true,orientation_scale_draws:true,sanitizer_runtime_negative:($mode=="sanitizer"),
      address_undefined_leak_checks:($mode=="sanitizer"),vptr_instrumentation:false,complete_native_catalog:false,
      complete_android_build:false,shipping_gui:false,combined_vm:false,physical_device:false}}' > "$work/verification.json"
echo "Focused $mode text evidence retained privately; complete image/GUI/VM acceptance remains separate."
