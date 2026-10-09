#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Host-only controls of bounded sysfs observation and actual patched GUI paths.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_tree="$component/src/upstream/orangefox-android16/bootable/recovery"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
work=$(mktemp -d "$component/build/private-scratch/telemetry-check-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/source/gui" "$work/base/gui"
for path in twrp.cpp data.cpp gui/battery.cpp; do
    cp -- "$source_tree/$path" "$work/source/$path"
done
# Reconstruct the actual reviewed predecessor, rather than reverse-applying
# 0053 to the final tree. Later patches can legitimately share its context
# (the RTC hook sits inside the same monitor loop).
index="$work/predecessor-index"
GIT_INDEX_FILE="$index" git -C "$source_tree" read-tree HEAD
found=false
while IFS= read -r patch; do
    if [[ $patch == 0053-bounded-uke-battery-cpu-telemetry.patch ]]; then found=true; break; fi
    [[ $patch =~ ^[0-9]{4}-[a-z0-9-]+\.patch$ ]]
    GIT_INDEX_FILE="$index" git -C "$source_tree" apply --cached --unidiff-zero "$component/patches/$patch"
done < "$component/configs/recovery-patches.list"
$found
for path in twrp.cpp data.cpp gui/battery.cpp; do
    GIT_INDEX_FILE="$index" git -C "$source_tree" show ":$path" > "$work/base/$path"
done
if ! rg -q TW_UKE_BOUNDED_TELEMETRY "$work/source/gui/battery.cpp"; then
    (cd "$work/source" && patch --batch --forward -p1 < "$component/patches/0053-bounded-uke-battery-cpu-telemetry.patch")
fi
extract() {
    local tree=$1 output=$2
    mkdir -p "$output"
    awk '/^int GUIBattery::Render\(void\)/ {copy=1} /^int GUIBattery::Update\(void\)/ {exit} copy {print}' "$tree/gui/battery.cpp" > "$output/telemetry-render.inc"
    awk '/auto monitorBatteryInBackground = / {copy=1} copy {print} copy && /^\t};/ {exit}' "$tree/twrp.cpp" > "$output/telemetry-monitor.inc"
    awk '/^\telse if \(varName == "tw_cpu_temp"/ {copy=1} copy && /^\treturn -1;/ {exit} copy {print}' "$tree/data.cpp" > "$output/telemetry-cpu.inc"
    for fragment in telemetry-render telemetry-monitor telemetry-cpu; do [[ -s $output/$fragment.inc ]]; done
}
extract "$work/source" "$work"
extract "$work/base" "$work/base-fragments"
rg -q 'ure::telemetry::observe_battery' "$work/telemetry-monitor.inc"
rg -q 'ure::telemetry::cpu_snapshot' "$work/telemetry-cpu.inc"
rg -q 'ure::telemetry::fill_height' "$work/telemetry-render.inc"
# The Uke macro is opt-in: preprocess every changed function without it and
# compare to the baseline, ignoring formatting introduced by directive lines.
for fragment in telemetry-render telemetry-monitor telemetry-cpu; do
    "$compiler" -E -P -x c++ "$work/$fragment.inc" | tr -d '[:space:]' > "$work/$fragment.off"
    "$compiler" -E -P -x c++ "$work/base-fragments/$fragment.inc" | tr -d '[:space:]' > "$work/$fragment.base-off"
    cmp -- "$work/$fragment.off" "$work/$fragment.base-off"
done
flags=(-std=c++17 -Wall -Wextra -Werror -UNDEBUG -O1 -pthread -DTW_UKE_BOUNDED_TELEMETRY -I "$work" -I "$component/src/device/xiaomi/uke")
for flavor in native sanitized; do
    instrumentation=()
    if [[ $flavor == sanitized ]]; then instrumentation=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g); fi
    timeout 60 "$compiler" "${flags[@]}" "${instrumentation[@]}" "$component/tests/ure/telemetry.cpp" -o "$work/test-$flavor"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        timeout 30 "$work/test-$flavor" "$work/fixture-$flavor/root"
done
# A missing containment flag and missing unavailable-render override must each
# be detected by the same functional fixture, not by a source-string assertion.
mkdir -p "$work/mutant-containment" "$work/mutant-render"
sed 's/how.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS;/how.resolve = 0;/' "$component/src/device/xiaomi/uke/ure-telemetry.hpp" > "$work/mutant-containment/ure-telemetry.hpp"
timeout 60 "$compiler" -I "$work/mutant-containment" "${flags[@]}" "$component/tests/ure/telemetry.cpp" -o "$work/removed-containment"
if timeout 30 "$work/removed-containment" "$work/mutant-containment/fixture/root" > "$work/mutant-containment.log" 2>&1; then
    echo 'Removed containment guard unexpectedly passed' >&2; exit 1
fi
rg -q '^FAIL: outside-root class alias accepted$' "$work/mutant-containment.log"
cp -- "$work/telemetry-monitor.inc" "$work/telemetry-cpu.inc" "$work/mutant-render/"
sed '/if (!available) mBatteryPercentStr = "--";/d' "$work/telemetry-render.inc" > "$work/mutant-render/telemetry-render.inc"
timeout 60 "$compiler" -I "$work/mutant-render" "${flags[@]}" "$component/tests/ure/telemetry.cpp" -o "$work/removed-render-clear"
if timeout 30 "$work/removed-render-clear" "$work/mutant-render/fixture/root" > "$work/mutant-render.log" 2>&1; then
    echo 'Removed unavailable render override unexpectedly passed' >&2; exit 1
fi
rg -q '^FAIL: unavailable render showed percent fill or charging$' "$work/mutant-render.log"
printf '%s\n' 'Telemetry native/ASAN/UBSAN, exact production GUI/monitor/CPU paths, non-Uke preprocessor preservation and negative controls passed. Hardware/provider acceptance remains separate.'
