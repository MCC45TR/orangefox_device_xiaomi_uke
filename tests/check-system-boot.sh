#!/usr/bin/env bash
# Exercise the production reboot selector helper on disposable regular files.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$component/build/system-boot-tests"
work=$(mktemp -d "$component/build/system-boot-tests/run-XXXXXXXX")
flags=(-std=c++20 -O1 -g -Wall -Wextra -Werror -DURE_HOST_POLICY_FIXTURE
    -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer
    -I"$component/tests/ure/misc-policy/mocks" -I"$component/src/device/xiaomi/uke/recoveryctl/libuke")
compiler=${CXX:-$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++}
"$compiler" "${flags[@]}" "$component/tests/ure/system_boot.cpp" \
    -Wl,--wrap=open -Wl,--wrap=pread -Wl,--wrap=pwrite -Wl,--wrap=fsync -o "$work/check"
mkdir -m 0700 "$work/fixture"
timeout 45 "$work/check" "$work/fixture"
# Both production callers must prepare before they acknowledge or request boot.
tree="$component/src/upstream/orangefox-android16"
awk '/int TWFunc::tw_reboot\(/,/^}$/ {print}' "$tree/bootable/recovery/twrp-functions.cpp" > "$work/tw-reboot.cpp"
awk '/bool RebootHandler\(/,/^}$/ {print}' "$tree/system/core/fastboot/device/commands.cpp" > "$work/fastboot-reboot.cpp"
for file in "$work/tw-reboot.cpp" "$work/fastboot-reboot.cpp"; do
    [[ $(rg -c 'ure::prepare_system_boot\(lifecycle\)' "$file") == 1 ]]
    awk '/prepare_system_boot\(lifecycle\)/ {prepared=1}
         /"reboot,"|"reboot,from_fastboot"/ {if(!prepared)exit 1; found=1}
         END {if(!prepared || !found)exit 1}' "$file"
done
printf 'System reboot regressions passed; host fixtures only.\n'
