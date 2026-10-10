#!/usr/bin/env bash
# Stable source identities for the native host fixture gate.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$component"
{
    find src/device/xiaomi/uke/recoveryctl tests/ure -type f -print0
    find src/device/xiaomi/uke/touch tests/touch-session -type f -print0
    printf '%s\0' src/device/xiaomi/uke/ure-touch.hpp tests/check-touch-session.sh scripts/check-touch-payload.sh scripts/touch-payload-init.sh \
        patches/0062-own-the-uke-touch-session.patch \
        src/upstream/orangefox-android16/bootable/recovery/gui/Android.bp
    find src/upstream/orangefox-android16/bootable/recovery/touch -type f -print0
    find src/upstream/orangefox-android16/bootable/recovery/gui/touch -type f -print0
    printf '%s\0' src/upstream/orangefox-android16/bootable/recovery/ure-touch.hpp
    printf '%s\0' patches/0063-persist-bounded-recovery-ui-preferences.patch \
        tests/check-ui-preferences.sh \
        src/upstream/orangefox-android16/bootable/recovery/infomanager.hpp \
        src/upstream/orangefox-android16/bootable/recovery/data.hpp
    printf '%s\0' patches/0064-preserve-unchanged-touch-coordinates.patch \
        patches/0065-keep-scroll-viewport-above-navigation.patch tests/check-theme-viewport.sh \
        patches/0066-synchronize-initial-touch-panel-wake.patch \
        patches/0067-skip-plane-updates-while-panel-is-blanked.patch \
        tests/check-touch-release.sh tests/check-touch-device-state.sh
    printf '%s\0' src/device/xiaomi/uke/maintainer.xml
    printf '%s\0' src/device/xiaomi/uke/{twrp_uke.mk,system.prop,ure-device-identity.hpp} \
        patches/0061-describe-uke-device-and-soc.patch
    printf '%s\0' scripts/native-inputs.sh tests/run-native.sh tests/check-ure.sh \
        tests/check-recoveryctl.sh tests/check-installer.sh tests/check-payload.sh \
        tests/check-nested-payloads.sh tests/check-payload-fixtures.sh tests/check-backup.sh tests/check-storage-backup.sh tests/check-restore.sh tests/check-stream-restore.sh tests/check-gpt.sh tests/check-stock-gpt.sh tests/check-partition-map.sh scripts/receive-backup.sh scripts/restore-from-host.sh
    printf '%s\0' tests/check-display.sh tests/generate-display-hooks.sh src/device/xiaomi/uke/ure-gui.cpp \
        patches/0006-tablet-interface-density.patch patches/0011-literal-ure-theme-defaults.patch patches/0012-responsive-stock-theme.patch src/upstream/orangefox-android16/bootable/recovery/gui/pages.cpp \
        src/upstream/orangefox-android16/bootable/recovery/gui/gui.cpp src/upstream/orangefox-android16/bootable/recovery/gui/listbox.cpp
    printf '%s\0' src/upstream/orangefox-android16/bootable/recovery/gui/theme/portrait_hdpi/{resources/{vars,images}.xml,pages/{files,settings}.xml,pages/templates/navbar.xml,splash.xml,themes/sed/{splash,splash_orig}.xml}
    printf '%s\0' patches/0014-preserve-hid-report-boundaries.patch patches/0015-menu-list-default-scroll.patch patches/0016-described-recovery-menus.patch patches/0017-early-extra-navigation-resources.patch patches/0018-scale-preview-widget.patch patches/0019-scaled-monitor-output.patch
    find src/device/xiaomi/uke/ui-icons -type f -print0
    printf '%s\0' tests/check-menu-rendering.sh scripts/check-ui-icons.sh tests/generate-scale-preview-hooks.sh
    printf '%s\0' src/upstream/orangefox-android16/bootable/recovery/gui/scrolllist.cpp src/upstream/orangefox-android16/bootable/recovery/gui/theme/portrait_hdpi/pages/templates/base.xml src/upstream/orangefox-android16/bootable/recovery/gui/theme/portrait_hdpi/pages/main.xml
    printf '%s\0' tests/check-tree-backup.sh
    printf '%s\0' tests/check-filesystems.sh tests/check-rescue.sh tests/check-boot-audit.sh tests/check-btrfs-vm.sh \
        tests/generate-management-hooks.sh src/device/xiaomi/uke/device.mk src/device/xiaomi/uke/ure-tools.lock.json src/device/xiaomi/uke/prepare-public-ramdisk.sh \
        scripts/prepare-build-tree.sh scripts/prepare-text-layout-sources.sh tests/check-host-prerequisites.sh scripts/build-public.sh patches/0009-native-boot-audit-codecs.patch \
        patches/0004-link-native-ure.patch patches/0041-chain-recovery-packaging-hooks.patch tests/check-recovery-packaging-hooks.sh src/upstream/orangefox-android16/bootable/recovery/Android.mk \
        src/upstream/orangefox-android16/external/zstd/Android.bp src/upstream/orangefox-android16/external/libncurses/Android.bp
    printf '%s\0' tests/check-layout.sh tests/check-dualboot-vm.sh tests/check-formatter-handoff-vm.sh \
        tests/vm/formatter-handoff.cpp tests/vm/strip-debug-module.sh \
        tests/generate-layout-hooks.sh src/device/xiaomi/uke/maintainer.xml patches/0008-partition-layout-graph.patch
    printf '%s\0' tests/check-partition-job.sh tests/check-partition-job-vm.sh tests/check-sanitizers.sh scripts/with-host-budget.sh scripts/host-ccache.sh
    printf '%s\0' patches/0013-soong-host-memory-policy.patch src/upstream/orangefox-android16/build/soong/ui/build/soong.go
    printf '%s\0' scripts/host-budget-policy.sh scripts/run-host-budget-job.sh tests/check-host-budget.sh \
        tests/check-host-builder-patches.sh patches/0028-soong-host-parallelism.patch patches/0029-blueprint-host-parallelism.patch \
        patches/0034-blueprint-bounded-graph-workers.patch \
        patches/0035-blueprint-sparse-provider-storage.patch patches/0036-blueprint-map-value-hash.patch \
        tests/blueprint/provider_retention_test.go tests/blueprint/hash_regression_test.go \
        configs/soong-patches.list configs/blueprint-patches.list \
        src/upstream/orangefox-android16/build/blueprint/{context.go,provider.go,module_ctx.go,proptools/hash_provider.go,bootstrap/{command.go,bootstrap.go},microfactory/microfactory.go}
    printf '%s\0' scripts/host-temp-policy.sh scripts/with-android-build-environment.sh scripts/run-android-build-job.sh tests/check-host-temp.sh
    printf '%s\0' scripts/index-build-tree.sh scripts/build-evidence-lib.sh scripts/build-evidence.sh \
        src/host/publish-build-directory.cpp tests/check-build-evidence.sh scripts/audit-recovery-image.sh scripts/package-prerelease.sh scripts/describe-prerelease.sh scripts/archive-release-sources.sh
    printf '%s\0' scripts/check-packed-payload.sh tests/check-packed-payload.sh
    printf '%s\0' scripts/check-ui-resource-parity.sh tests/check-ui-resource-parity.sh
    printf '%s\0' configs/release-policy.json scripts/release-policy-lib.sh scripts/release-policy.sh \
        scripts/native-test-catalog.sh scripts/check-package-repeat.sh tests/check-release-policy.sh
    printf '%s\0' tests/check-stock-job.sh scripts/describe-stock-payloads.sh manifests/stock-payloads-global.json
    printf '%s\0' tests/installer_test.cpp tests/check-stock-boot-programming.sh scripts/describe-stock-boot-programming.sh manifests/stock-boot-programming-global.json
    printf '%s\0' tests/check-boot-router.sh tests/check-aarch64.sh
    printf '%s\0' tests/check-drm-surface.sh patches/0010-drm-framebuffer-initialization.patch
    printf '%s\0' tests/check-write-gate.sh tests/generate-write-gate-hooks.sh configs/ure/legacy-write-entry-points.tsv \
        patches/0020-shared-recovery-write-gate.patch patches/0021-fastbootd-write-gate.patch src/device/xiaomi/uke/ure-write-gate.hpp \
        src/device/xiaomi/uke/BoardConfig.mk src/device/xiaomi/uke/recovery/root/system/etc/recovery.fstab
    printf '%s\0' patches/0037-guard-misc-message-writes.patch patches/0038-guard-boot-control-writes.patch \
        configs/boot-control-patches.list tests/check-write-gate-vm.sh tests/check-misc-write-policy.sh \
        src/upstream/orangefox-android16/bootable/recovery/bootloader_message/{Android.bp,bootloader_message.cpp,include/bootloader_message/bootloader_message.h} \
        src/upstream/orangefox-android16/bootable/recovery/install/get_args.cpp \
        src/upstream/orangefox-android16/hardware/interfaces/boot/1.1/default/boot_control/{Android.bp,libboot_control.cpp,include/libboot_control/libboot_control.h,include/private/boot_control_definition.h}
    printf '%s\0' src/upstream/orangefox-android16/hardware/interfaces/boot/1.1/default/{BootControl.cpp,BootControl.h}
    printf '%s\0' patches/0042-refuse-unaccepted-boot-hal-resolution.patch patches/0043-system-only-boot-hal-library-path.patch \
        patches/0044-read-extra-logical-metadata-records.patch patches/0045-use-declared-terminfo-install-targets.patch \
        patches/0046-dualboot-recovery-only-reboot.patch patches/0047-dualboot-fastbootd-recovery-only-reboot.patch \
        tests/check-lp-record-reader.sh tests/f2fs-metadata-oracle-lib.sh tests/check-f2fs-metadata-oracle.sh \
        tests/check-boot-hal-admission.sh tests/check-packed-startup-refusals.sh \
        tests/packed-startup-trace-lib.sh tests/check-packed-startup-trace-oracle.sh \
        src/upstream/orangefox-android16/hardware/interfaces/boot/{1.0,1.1,1.2}/default/{Android.bp,service.cpp} \
        src/upstream/orangefox-android16/hardware/interfaces/boot/aidl/client/{Android.bp,BootControlClient.cpp} \
        src/upstream/orangefox-android16/system/extras/bootctl/{Android.bp,bootctl.cpp} \
        src/upstream/orangefox-android16/bootable/recovery/etc/init/android.hardware.boot@{1.0,1.1,1.2}-service.rc \
        src/upstream/orangefox-android16/bootable/recovery/prebuilt/Android.mk
    printf '%s\0' tests/check-recovery-first-stage.sh tests/check-recovery-first-stage-cmdline.sh patches/0039-preserve-recovery-first-stage-root.patch \
        src/upstream/orangefox-android16/system/core/init/first_stage_init.cpp \
        src/upstream/orangefox-android16/system/libbase/include/android-base/unique_fd.h
    printf '%s\0' tests/check-recovery-module-policy.sh patches/0040-recovery-block-automatic-storage-modules.patch \
        src/upstream/orangefox-android16/system/core/libmodprobe/{libmodprobe.cpp,libmodprobe_ext.cpp,exthandler.cpp} \
        src/upstream/orangefox-android16/system/core/libmodprobe/include/{modprobe/modprobe.h,exthandler/exthandler.h}
    printf '%s\0' src/device/xiaomi/uke/ure-readonly-fstab-import.hpp patches/0048-readonly-vendor-fstab-import.patch \
        tests/check-readonly-fstab-import.sh src/upstream/orangefox-android16/bootable/recovery/partitionmanager.cpp \
        src/upstream/orangefox-android16/bootable/recovery/ure-readonly-fstab-import.hpp
    printf '%s\0' patches/0049-serialize-fox-command-admission.patch tests/check-fox-command-admission.sh \
        src/upstream/orangefox-android16/bootable/recovery/fox_fifo/fox_remote_state.cpp
    printf '%s\0' configs/vold-patches.list patches/0050-vold-optional-key-upgrade.patch tests/check-vold-key-upgrade.sh \
        src/upstream/orangefox-android16/system/vold/KeyStorage.cpp src/upstream/orangefox-android16/system/vold/Keystore.h
    printf '%s\0' patches/0051-preserve-tracking-id-touch-release.patch tests/check-touch-release.sh \
        tests/ure/touch_release.cpp tests/ure/fixtures/touch-four-contacts.events \
        src/upstream/orangefox-android16/bootable/recovery/minuitwrp/events.cpp
    printf '%s\0' patches/0052-isolate-touch-device-parser-state.patch tests/check-touch-device-state.sh \
        patches/0053-bounded-uke-battery-cpu-telemetry.patch patches/0058-propagate-uke-telemetry-build-flags.patch \
        src/device/xiaomi/uke/ure-telemetry.hpp tests/check-telemetry.sh \
        src/upstream/orangefox-android16/bootable/recovery/{ure-telemetry.hpp,data.cpp,gui/battery.cpp,gui/libfoxui_defaults.go,orangefox_soong.mk} \
        src/upstream/orangefox-android16/vendor/twrp/config/BoardConfigSoong.mk \
        patches/0054-bound-synthetic-password-records.patch tests/check-fbe-parser.sh \
        src/upstream/orangefox-android16/system/vold/{Decrypt.cpp,ure-fbe-parser.hpp,ure-fbe-gcm.hpp} \
        patches/0055-authenticate-synthetic-password-gcm.patch tests/check-fbe-gcm.sh
    printf '%s\0' patches/0059-bound-existing-protector-weaver-reads.patch tests/check-weaver.sh \
        src/upstream/orangefox-android16/system/vold/{Weaver1.cpp,Weaver1.h,ure-weaver-policy.hpp}
    printf '%s\0' patches/0060-use-parent-paths-for-gui-support-headers.patch \
        patches/0057-read-validated-uke-rtc-offset.patch src/device/xiaomi/uke/ure-clock.hpp tests/check-clock.sh \
        src/upstream/orangefox-android16/bootable/recovery/ure-clock.hpp \
        patches/0056-native-session-theme-application.patch src/device/xiaomi/uke/ure-theme.hpp \
        src/upstream/orangefox-android16/bootable/recovery/ure-theme.hpp tests/check-native-theme.sh tests/check-reviewed-additions.sh \
        src/upstream/orangefox-android16/bootable/recovery/gui/theme/portrait_hdpi/pages/{customization.xml,templates/templates.xml}
    find src/device/xiaomi/uke/clock-sync -type f -print0
    printf '%s\0' tests/check-clock-mount.sh tests/check-clock-mount-vm.sh
    find src/upstream/orangefox-android16/bootable/recovery/gui/theme/portrait_hdpi/themes/{styles,sed} -type f -print0
    find src/device/xiaomi/uke/recovery/root src/device/xiaomi/uke/sepolicy -type f -print0
    printf '%s\0' tests/check-recovery-startup.sh
    printf '%s\0' tests/generate-text-decoder.sh patches/0022-bounded-utf8-text.patch \
        src/upstream/orangefox-android16/bootable/recovery/minuitwrp/truetype.cpp \
        src/upstream/orangefox-android16/bootable/recovery/minuitwrp/include/minuitwrp/truetype.hpp
    printf '%s\0' tests/generate-text-hooks.sh tests/check-text-patches.sh patches/0023-bounded-text-raster-and-cache.patch \
        scripts/prepare-recovery-patches.sh configs/recovery-patches.list \
        src/upstream/orangefox-android16/bootable/recovery/minuitwrp/graphics_utils.cpp \
        src/upstream/orangefox-android16/external/roboto-fonts/RobotoStatic-Regular.ttf \
        src/upstream/orangefox-android16/external/freetype/{Android.bp,CMakeLists.txt,LICENSE.TXT,builds/cmake/FindHarfBuzz.cmake}
    printf '%s\0' tests/with-operation-coordinator.sh tests/generate-lifecycle-hooks.sh \
        src/device/xiaomi/uke/ure-lifecycle.hpp patches/0024-shared-operation-lifecycle.patch \
        patches/0025-fastbootd-operation-lifecycle.patch configs/fastboot-patches.list \
        scripts/prepare-reviewed-patches.sh scripts/prepare-fastboot-patches.sh \
        src/upstream/orangefox-android16/bootable/recovery/partitions.hpp
    printf '%s\0' tests/check-device-profile.sh
    printf '%s\0' tests/check-partition-capabilities.sh
    printf '%s\0' tests/check-platform.sh tests/check-platform-arm64.sh
    printf '%s\0' tests/check-functional-vm.sh tests/vm/functional-init.sh
    printf '%s\0' tests/prepare-gui-vm.sh tests/check-gui-vm.sh tests/gui-vm-control.sh \
        tests/vm/{gui-ashmem.cpp,gui-properties.cpp,gui-property-info.cpp,lp-dump.cpp}
    printf '%s\0' tests/check-populated-filesystems.sh
    printf '%s\0' tests/with-rescue-cgroup.sh patches/0026-owned-management-jobs.patch patches/0027-join-management-jobs-before-teardown.patch
    find src/upstream/orangefox-android16/external/freetype/{src,include,builds/unix} -type f -print0
    find src/upstream/orangefox-android16/bootable/recovery/libpixelflinger/include -type f -print0
    printf '%s\0' src/upstream/orangefox-android16/bootable/recovery/gui/theme/common/languages/en.xml
    printf '%s\0' src/upstream/orangefox-android16/bootable/recovery/{Android.bp,twrp.cpp,partition.cpp,partitionmanager.cpp,twrp-functions.cpp,openrecoveryscript.cpp,ure-write-gate.hpp} \
        src/upstream/orangefox-android16/bootable/recovery/{install/{install.cpp,wipe_data.cpp,adb_install.cpp},twrpinstall/{install.cpp,twinstall.cpp,adb_install.cpp},recovery_utils/roots.cpp,gui/action.cpp,twrpRepacker.cpp} \
        src/upstream/orangefox-android16/system/core/fastboot/{Android.bp,device/{commands.cpp,fastboot_device.cpp,utility.cpp,variables.cpp,utility.h}}
    printf '%s\0' src/device/xiaomi/uke/display-mirror.hpp src/device/xiaomi/uke/display-mirror.cpp \
        src/device/xiaomi/uke/display-mirror-layout.cpp patches/0007-usb-monitor-and-input.patch \
        tests/generate-input-hooks.sh tests/generate-events-hooks.sh
    printf '%s\0' src/upstream/orangefox-android16/bootable/recovery/minuitwrp/{events.cpp,graphics.cpp,graphics_drm.cpp,Android.bp,include/minuitwrp/minui.h} \
        src/upstream/orangefox-android16/bootable/recovery/minuitwrp/{display-mirror.cpp,display-mirror-layout.cpp,display-mirror.hpp} \
        src/upstream/orangefox-android16/bootable/recovery/gui/{hardwarekeyboard.cpp,objects.hpp,mousecursor.cpp}
    printf '%s\0' src/upstream/orangefox-android16/external/libdrm/{xf86drm.h,xf86drmMode.h,include/drm/drm.h,include/drm/drm_mode.h,include/drm/drm_fourcc.h}
} | sort -z | xargs -0 sha256sum
# This comment is a composite inventory identity, not a single file checksum.
# It includes every supported language and optional owner draft, dependencies,
# fonts/licenses, directory membership, modes, links and exact source pins.
localization_identity=$(bash scripts/localization-evidence.sh source | sha256sum | cut -d' ' -f1)
printf '# localization_source_sha256=%s\n' "$localization_identity"
