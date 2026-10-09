#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Host synthetic records only; never calls clock_settime or an RTC ioctl.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
work=$(mktemp -d "$component/build/clock-check-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
for flavor in native sanitized; do
    flags=()
    [[ $flavor != sanitized ]] || flags=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g)
    timeout 45 "$compiler" -std=c++20 -Wall -Wextra -Werror -UNDEBUG -O1 -pthread "${flags[@]}" \
        -I "$component/src/device/xiaomi/uke" "$component/tests/ure/clock.cpp" -o "$work/$flavor"
    mkdir "$work/$flavor-fixture"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 "$work/$flavor" "$work/$flavor-fixture"
done
# The production helper has no fallback mount, daemon, timezone or RTC writer.
if rg -n '::mount|::ioctl|settimeofday|O_RDWR|O_WRONLY|time_daemon\(' "$component/src/device/xiaomi/uke/ure-clock.hpp"; then exit 1; fi
rg -q 'mounted.st_dev != device.st_rdev' "$component/src/device/xiaomi/uke/ure-clock.hpp"
rg -q 'flags.f_flag & ST_RDONLY' "$component/src/device/xiaomi/uke/ure-clock.hpp"
printf '%s\n' 'Clock host and sanitizer controls passed; no host clock modification occurred.'
