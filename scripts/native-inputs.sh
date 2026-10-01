#!/usr/bin/env bash
# Stable source identities for the native host fixture gate.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$component"
{
    find src/device/xiaomi/uke/recoveryctl tests/ure -type f -print0
    printf '%s\0' scripts/native-inputs.sh tests/run-native.sh tests/check-ure.sh \
        tests/check-recoveryctl.sh tests/check-installer.sh tests/check-payload.sh \
        tests/check-nested-payloads.sh tests/check-payload-fixtures.sh tests/check-backup.sh tests/check-storage-backup.sh tests/check-restore.sh tests/check-stream-restore.sh tests/check-gpt.sh tests/check-stock-gpt.sh tests/check-partition-map.sh scripts/receive-backup.sh scripts/restore-from-host.sh
    printf '%s\0' tests/check-display.sh tests/generate-display-hooks.sh src/device/xiaomi/uke/ure-gui.cpp \
        patches/0006-tablet-interface-density.patch src/upstream/orangefox-android16/bootable/recovery/gui/pages.cpp \
        src/upstream/orangefox-android16/bootable/recovery/gui/gui.cpp
    printf '%s\0' tests/check-tree-backup.sh
    printf '%s\0' src/device/xiaomi/uke/display-mirror.hpp src/device/xiaomi/uke/display-mirror.cpp \
        src/device/xiaomi/uke/display-mirror-layout.cpp patches/0007-usb-monitor-and-input.patch \
        tests/generate-input-hooks.sh tests/generate-events-hooks.sh
    printf '%s\0' src/upstream/orangefox-android16/bootable/recovery/minuitwrp/{events.cpp,graphics.cpp,graphics_drm.cpp,Android.bp,include/minuitwrp/minui.h} \
        src/upstream/orangefox-android16/bootable/recovery/minuitwrp/{display-mirror.cpp,display-mirror-layout.cpp,display-mirror.hpp} \
        src/upstream/orangefox-android16/bootable/recovery/gui/{hardwarekeyboard.cpp,objects.hpp,mousecursor.cpp}
    printf '%s\0' src/upstream/orangefox-android16/external/libdrm/{xf86drm.h,xf86drmMode.h,include/drm/drm.h,include/drm/drm_mode.h,include/drm/drm_fourcc.h}
} | sort -z | xargs -0 sha256sum
