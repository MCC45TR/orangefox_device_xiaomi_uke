#!/usr/bin/env bash
# Host-only pinned Clang instrumentation. No target runtime or physical claim.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" sanitizer bash "$component/tests/check-sanitizers.sh" "$@"
fi
jobs=${UKE_HOST_JOBS:-2}
[[ $jobs =~ ^([1-9]|1[0-6])$ ]]
cd "$component"
export CCACHE_DIR=${CCACHE_DIR:-"$component/build/ccache/native"}
mkdir -p "$CCACHE_DIR"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
expected=55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f
[[ -x $compiler && $(sha256sum "$compiler" | cut -d' ' -f1) == "$expected" ]]
mkdir -p reports/private
bash scripts/native-inputs.sh > reports/private/partition-sanitizer-inputs.sha256
cmake -S src/device/xiaomi/uke/recoveryctl -B build/ure-sanitized-clang -G Ninja \
    -DCMAKE_CXX_COMPILER="$compiler" -DCMAKE_C_COMPILER="${compiler%++}" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$component/scripts/host-ccache.sh" \
    '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer' \
    '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr'
cmake --build build/ure-sanitized-clang -j"$jobs"
count=$(ctest --test-dir build/ure-sanitized-clang --show-only=json-v1 | jq -er '.tests | length')
[[ $count -ge 24 ]]
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    ctest --test-dir build/ure-sanitized-clang --output-on-failure
UKE_DRM_SANITIZER=1 bash tests/check-drm-surface.sh
cmp <(bash scripts/native-inputs.sh) reports/private/partition-sanitizer-inputs.sha256
jq -n --arg inputs "$(sha256sum reports/private/partition-sanitizer-inputs.sha256 | cut -d' ' -f1)" \
    --arg compiler "$expected" --argjson count "$count" \
    '{schema_version:1,native_test_inputs_sha256:$inputs,compiler_sha256:$compiler,ctest_executable_count:$count,
      validation:{address_sanitizer:true,undefined_behavior_sanitizer:true,leak_detection:true,halt_on_error:true,vptr_instrumentation:false,physical_device:false}}' \
    > reports/private/partition-sanitizer-verification.json
printf '%s\n' 'Pinned Clang address/undefined/leak gates passed; vptr instrumentation remains excluded by the pinned host runtime limitation.'
