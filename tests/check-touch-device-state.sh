#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Exact per-device minuitwrp controls; no tablet I/O or active source mutation.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-check}
[[ $mode == check || $mode == candidate ]]
source_file="$component/src/upstream/orangefox-android16/bootable/recovery/minuitwrp/events.cpp"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
mkdir -p "$component/build"
work=$(mktemp -d "$component/build/touch-device-state-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/minuitwrp"
cp -- "$source_file" "$work/minuitwrp/events.cpp"
if [[ $mode == candidate ]]; then
    if ! rg -q 'if \(touchReleaseOnNextSynReport != 2\)' "$work/minuitwrp/events.cpp"; then
        (cd "$work" && patch --batch --forward -p1 < "$component/patches/0051-preserve-tracking-id-touch-release.patch")
    fi
    if ! rg -q '^struct touch_state \{' "$work/minuitwrp/events.cpp"; then
        (cd "$work" && patch --batch --forward -p1 < "$component/patches/0052-isolate-touch-device-parser-state.patch")
    fi
fi
[[ $(rg -c 'if \(touchReleaseOnNextSynReport != 2\)' "$work/minuitwrp/events.cpp") == 2 ]]
rg -q '^struct touch_state \{' "$work/minuitwrp/events.cpp"
rg -q 'evs\[ev_count\] = \{\};' "$work/minuitwrp/events.cpp"
rg -q 'e->vk = \{\};' "$work/minuitwrp/events.cpp"
if sed -n '/^static int vk_modify(/,/^int ev_get(/p' "$work/minuitwrp/events.cpp" | rg -q 'static int (downX|discard|last_virt_key|lastWasSynReport|touchReleaseOnNextSynReport|use_tracking_id_negative_as_touch_release)'; then
    echo 'Parser protocol state must not be process-global' >&2; exit 1
fi
generate() {
    local source=$1 output=$2
    { printf '%s\n' '#include "events-hooks.h"'; awk '/^#define MAX_DEVICES/ {copy=1} copy {print}' "$source"; } > "$output"
}
generate "$work/minuitwrp/events.cpp" "$work/events-hooks.cpp"
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-macro-redefined -UNDEBUG -O1 -I "$work" -I "$component/tests/ure")
for flavor in native sanitized; do
    instrumentation=()
    if [[ $flavor == sanitized ]]; then instrumentation=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g); fi
    timeout 45 "$compiler" "${flags[@]}" "${instrumentation[@]}" "$component/tests/ure/touch_device_state.cpp" -o "$work/test-$flavor"
    for scenario in empty interference interleaved compatibility virtual-keys dropped rescan; do
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
            timeout 20 "$work/test-$flavor" "$scenario"
    done
    # Keep the independent 0051 fixture/order and all four legacy controls.
    awk '/^enum \{/ {copy=1} /^static struct pollfd/ {exit} copy {print}' "$work/minuitwrp/events.cpp" > "$work/touch-vk-types.inc"
    awk '/^static int vk_tp_to_screen\(/ {copy=1} /^int ev_get\(/ {exit} copy {print}' "$work/minuitwrp/events.cpp" > "$work/touch-vk-modify.inc"
    timeout 45 "$compiler" "${flags[@]}" "${instrumentation[@]}" "$component/tests/ure/touch_release.cpp" -o "$work/release-$flavor"
    for scenario in fixture permutations single type-a pressure major isolated; do
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
            timeout 20 "$work/release-$flavor" "$scenario" "$component/tests/ure/fixtures/touch-four-contacts.events"
    done
done
reject_mutant() {
    local label=$1 source=$2 scenario=$3 message=$4
    local mutant_dir="$work/$label"
    mkdir "$mutant_dir"
    generate "$source" "$mutant_dir/events-hooks.cpp"
    timeout 45 "$compiler" "${flags[@]}" -I "$mutant_dir" -iquote "$mutant_dir" "$component/tests/ure/touch_device_state.cpp" -o "$mutant_dir/test"
    if "$mutant_dir/test" "$scenario" > "$mutant_dir/log" 2>&1; then
        printf 'Removed %s fix unexpectedly passed\n' "$label" >&2; exit 1
    fi
    rg -q -F "$message" "$mutant_dir/log"
}
# Restore shared protocol locals while retaining the inactive-release guard.
sed -E 's/int\& (downX) = e->vk.downX;/static int \1 = -1;/; s/int\& (downY) = e->vk.downY;/static int \1 = -1;/; s/int\& ([A-Za-z_]+) = e->vk\.[A-Za-z_]+;/static int \1 = 0;/' \
    "$work/minuitwrp/events.cpp" > "$work/shared.cpp"
reject_mutant shared "$work/shared.cpp" interference "FAIL: idle touch device released another device's contact"
reject_mutant shared-mode "$work/shared.cpp" compatibility "FAIL: another device's tracking mode disabled legacy release"
sed '/if (downX == -1 \&\& !discard)/,+1d' "$work/minuitwrp/events.cpp" > "$work/unguarded.cpp"
reject_mutant unguarded "$work/unguarded.cpp" empty "FAIL: initial empty touch device emitted an up edge"
sed 's/e->vk = {};/e->vk.downX = e->vk.downY = -1; e->vk.discard = e->vk.last_virt_key = e->vk.lastWasSynReport = e->vk.touchReleaseOnNextSynReport = 0;/' \
    "$work/minuitwrp/events.cpp" > "$work/stale-mode.cpp"
reject_mutant stale-mode "$work/stale-mode.cpp" dropped "FAIL: dropped stream retained learned tracking-ID release mode"
sed 's/evs\[ev_count\] = {};/memset(\&evs[ev_count], 0, sizeof(evs[ev_count]));/' "$work/minuitwrp/events.cpp" > "$work/zero-defaults.cpp"
reject_mutant zero-defaults "$work/zero-defaults.cpp" rescan "FAIL: rescanned empty slot emitted a phantom up"
printf '%s\n' 'Exact per-device parser native/ASAN/UBSAN and 0051 release controls passed; five removed-fix mutants rejected. GUI concurrent-contact arbitration and physical input acceptance remain separate.'
