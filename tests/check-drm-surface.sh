#!/usr/bin/env bash
# Compile actual renderer allocation functions against independent DRM stand-ins.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source=${UKE_DRM_SOURCE:-$component/src/upstream/orangefox-android16/bootable/recovery/minuitwrp/graphics_drm.cpp}
compiler=${CXX:-c++}
flags=()
if [[ ${UKE_DRM_SANITIZER:-0} == 1 ]]; then
    compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
    flags=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
work=$(mktemp -d "$component/build/drm-surface-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
awk '/^static void drm_destroy_surface\(/ { copying=1 } /^static drmModeCrtc.*find_crtc/ { exit } copying { print }' "$source" > "$work/drm-surface.inc"
rg -q '^static drm_surface \*drm_create_surface' "$work/drm-surface.inc"
awk '/^static GRSurface\* drm_flip\(/ { copying=1 } /^static void drm_exit\(/ { exit } copying { print }' "$source" > "$work/drm-flip.inc"
rg -q '^static GRSurface\* drm_flip' "$work/drm-flip.inc"
"$compiler" -std=c++20 -Wall -Wextra -Werror -ftrivial-auto-var-init=pattern "${flags[@]}" \
    -I"$work" -I"$component/src/upstream/orangefox-android16/external/libdrm" \
    -I"$component/src/upstream/orangefox-android16/external/libdrm/include/drm" \
    "$component/tests/ure/drm_surface.cpp" -o "$work/test"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$work/test"
