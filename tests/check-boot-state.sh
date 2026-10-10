#!/usr/bin/env bash
# Host-only measured boot-state controls; no properties or storage are changed.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler=$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
mkdir -p "$component/build"
work=$(mktemp -d "$component/build/boot-state-controls-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
"$compiler" -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize=vptr \
    -I"$component/src/device/xiaomi/uke/recoveryctl/libuke" "$component/tests/ure/boot_state.cpp" -o "$work/controls"
timeout 20 "$work/controls"
sed 's/return verified == "orange" \&\& seen\[0\];/return verified == "orange";/' \
    "$component/src/device/xiaomi/uke/recoveryctl/libuke/boot_state.hpp" > "$work/boot_state.hpp"
"$compiler" -std=c++20 -O1 -g -Wall -Wextra -Werror \
    -I"$work" "$component/tests/ure/boot_state.cpp" -o "$work/mutant"
ulimit -c 0
status=0
bash -c 'timeout 20 "$1"; exit "$?"' _ "$work/mutant" > "$work/mutant.log" 2>&1 || status=$?
[[ $status == 134 ]]
for source in preflight dualboot_device; do
    rg -qF 'bootloader_unlocked(system)' "$component/src/device/xiaomi/uke/recoveryctl/libuke/$source.cpp"
done
