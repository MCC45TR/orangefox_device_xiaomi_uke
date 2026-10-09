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
bash scripts/prepare-reviewed-patches.sh "$component/src/upstream/orangefox-android16/external/freetype" apply freetype
bash scripts/native-inputs.sh > reports/private/partition-sanitizer-inputs.sha256
cmake -S src/device/xiaomi/uke/recoveryctl -B build/ure-sanitized-clang -G Ninja \
    -DCMAKE_CXX_COMPILER="$compiler" -DCMAKE_C_COMPILER="${compiler%++}" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$component/scripts/host-ccache.sh" \
    '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer' \
    '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr'
cmake --build build/ure-sanitized-clang -j"$jobs"
bash scripts/native-test-catalog.sh build/ure-sanitized-clang > reports/private/partition-sanitizer-test-catalog.json
count=$(jq -er 'length' reports/private/partition-sanitizer-test-catalog.json)
[[ $count -ge 24 ]]
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    ctest --test-dir build/ure-sanitized-clang --output-on-failure
UKE_DRM_SANITIZER=1 bash tests/check-drm-surface.sh
bash tests/check-localization-keys.sh sanitizer
bash tests/check-gui-language.sh sanitizer
bash tests/check-localization-review.sh sanitizer
bash tests/check-platform.sh sanitizer
bash tests/check-readonly-fstab-import.sh
bash tests/check-fox-command-admission.sh
bash tests/check-vold-key-upgrade.sh
bash tests/check-touch-release.sh
bash tests/check-touch-device-state.sh
bash tests/check-touch-session.sh
bash tests/check-telemetry.sh
bash tests/check-fbe-parser.sh
bash tests/check-fbe-gcm.sh
bash tests/check-weaver.sh
bash tests/check-clock.sh
bash tests/check-clock-mount.sh
bash tests/check-native-theme.sh
cmp <(bash scripts/native-inputs.sh) reports/private/partition-sanitizer-inputs.sha256
jq -n --arg inputs "$(sha256sum reports/private/partition-sanitizer-inputs.sha256 | cut -d' ' -f1)" \
    --arg localization "$(sed -n 's/^# localization_source_sha256=//p' reports/private/partition-sanitizer-inputs.sha256)" \
    --arg compiler "$expected" --argjson count "$count" \
    --slurpfile catalog reports/private/partition-sanitizer-test-catalog.json \
    '{schema_version:1,native_test_inputs_sha256:$inputs,localization_inputs_sha256:$localization,compiler_sha256:$compiler,ctest_executable_count:$count,ctest_test_names:$catalog[0],
      validation:{localization_input_closure:true,host_localization_key_owner_schema_and_exact_header_closure:true,actual_gui_language_review_context:true,host_offline_translation_current_source_and_parent:true,platform_exact_context_contracts_and_pre_access_refusal:true,platform_read_only_health_and_hold_policy:true,platform_physical_backends_accepted:false,address_sanitizer:true,undefined_behavior_sanitizer:true,leak_detection:true,halt_on_error:true,vptr_instrumentation:false,per_device_touch_parser_and_release:true,bounded_telemetry_and_actual_gui_paths:true,bounded_fbe_record_and_kdf_controls:true,authenticated_fbe_gcm_and_cleanup:true,weaver_config_buffer_reply_and_retry_delay_controls:true,validated_rtc_offset_controls:true,native_session_theme_controls:true,physical_fbe_access:false,persistent_settings_accepted:false,fresh_image_touch_startup_accepted:false,physical_device:false}}' \
    | jq '.validation.private_rtc_mount_policy_controls=true' \
    > reports/private/partition-sanitizer-verification.json
printf '%s\n' 'Pinned Clang address/undefined/leak gates passed; vptr instrumentation remains excluded by the pinned host runtime limitation.'
