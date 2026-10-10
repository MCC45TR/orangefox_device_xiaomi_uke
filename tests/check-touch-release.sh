#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Exact vk_modify host controls; no tablet, donor execution, or device I/O.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-check}
[[ $mode == check || $mode == candidate ]]
source_file="$component/src/upstream/orangefox-android16/bootable/recovery/minuitwrp/events.cpp"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
mkdir -p "$component/build"
work=$(mktemp -d "$component/build/touch-release-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
cp -- "$source_file" "$work/events.cpp"
if [[ $mode == candidate ]] && ! rg -q 'if \(touchReleaseOnNextSynReport != 2\)' "$work/events.cpp"; then
    mkdir "$work/minuitwrp"
    mv "$work/events.cpp" "$work/minuitwrp/events.cpp"
    (cd "$work" && patch --batch --forward -p1 < "$component/patches/0051-preserve-tracking-id-touch-release.patch")
    mv "$work/minuitwrp/events.cpp" "$work/events.cpp"
fi
[[ $(rg -c 'if \(touchReleaseOnNextSynReport != 2\)' "$work/events.cpp") == 2 ]]
awk '/^enum \{/ {copy=1} /^static struct pollfd/ {exit} copy {print}' "$work/events.cpp" > "$work/touch-vk-types.inc"
awk '/^static int vk_tp_to_screen\(/ {copy=1} /^int ev_get\(/ {exit} copy {print}' "$work/events.cpp" > "$work/touch-vk-modify.inc"
rg -q '^static int vk_modify' "$work/touch-vk-modify.inc"
flags=(-std=c++17 -Wall -Wextra -Werror -UNDEBUG -O1 -I "$work")
for flavor in native sanitized; do
    instrumentation=()
    if [[ $flavor == sanitized ]]; then instrumentation=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g); fi
    timeout 45 "$compiler" "${flags[@]}" "${instrumentation[@]}" "$component/tests/ure/touch_release.cpp" -o "$work/test-$flavor"
    for scenario in fixture permutations single type-a pressure major repeated-position; do
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
            timeout 20 "$work/test-$flavor" "$scenario" "$component/tests/ure/fixtures/touch-four-contacts.events"
    done
done
mkdir "$work/mutant"
cp -- "$work/touch-vk-types.inc" "$work/mutant/"
sed '/if (touchReleaseOnNextSynReport != 2)/d' "$work/touch-vk-modify.inc" > "$work/mutant/touch-vk-modify.inc"
[[ $(rg -c 'if \(touchReleaseOnNextSynReport != 2\)' "$work/touch-vk-modify.inc") == 2 ]]
"$compiler" -std=c++17 -Wall -Wextra -Werror -UNDEBUG -O1 -I "$work/mutant" "$component/tests/ure/touch_release.cpp" -o "$work/removed-fix"
if "$work/removed-fix" fixture "$component/tests/ure/fixtures/touch-four-contacts.events" > "$work/mutant.log" 2>&1; then
    echo 'Removed touch-release fix unexpectedly passed' >&2; exit 1
fi
rg -q '^FAIL: tracking-ID release lost after trailing zeros$' "$work/mutant.log"
if rg -q '^struct touch_state \{' "$work/events.cpp"; then
    for flavor in native sanitized; do
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
            "$work/test-$flavor" isolated
    done
    printf '%s\n' 'Touch-release native/ASAN/UBSAN controls passed; removed fix rejected; per-device release isolation preserved.'
else
    set +e
    "$work/test-native" interference > "$work/interference.log" 2>&1
    interference_status=$?
    set -e
    [[ $interference_status == 10 ]]
    rg -q '^KNOWN_REMAINING: touchscreen state is shared across touch-capable devices$' "$work/interference.log"
    printf '%s\n' 'Touch-release native/ASAN/UBSAN controls passed; removed fix rejected. Separate touch-device shared-state risk remains and requires its own fix.'
fi
