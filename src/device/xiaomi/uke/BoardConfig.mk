# SPDX-License-Identifier: Apache-2.0
# Initial Uke recovery profile. Values here are limited to measured stock inputs.

DEVICE_PATH := device/xiaomi/uke

TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := generic
TARGET_CPU_VARIANT_RUNTIME := kryo300

TARGET_NO_BOOTLOADER := true
TARGET_BOOTLOADER_BOARD_NAME := uke
TARGET_BOARD_PLATFORM := pineapple
BOARD_USES_QCOM_HARDWARE := true

TARGET_KERNEL_ARCH := arm64
TARGET_KERNEL_HEADER_ARCH := arm64
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/kernel
BOARD_KERNEL_IMAGE_NAME := Image
BOARD_KERNEL_PAGESIZE := 4096
BOARD_BOOT_HEADER_VERSION := 4
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOT_HEADER_VERSION)
BOARD_MKBOOTIMG_ARGS += --pagesize $(BOARD_KERNEL_PAGESIZE)
BOARD_USES_GENERIC_KERNEL_IMAGE := true
BOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE := true
BOARD_RAMDISK_USE_LZ4 := true

# Dedicated kernel-less recovery omits INTERNAL_KERNEL_CMDLINE. Pass both
# safeguards through its own header while retaining all common mkbootimg args.
# rdinit selects the reviewed loader even if init_boot provides /init; the
# kernel blacklist also covers loaders that bypass libmodprobe. Header presence
# alone does not establish that a particular bootloader honors these arguments.
BOARD_RECOVERY_MKBOOTIMG_ARGS = $(BOARD_MKBOOTIMG_ARGS) --cmdline "rdinit=/system/bin/init module_blacklist=charger_partition,ufs_ffu"

AB_OTA_UPDATER := true
AB_OTA_PARTITIONS := \
    boot \
    dtbo \
    init_boot \
    odm \
    product \
    recovery \
    system \
    system_dlkm \
    system_ext \
    vbmeta \
    vbmeta_system \
    vendor \
    vendor_boot \
    vendor_dlkm

# Measured from Global OS3.0.303.0 rawprogram and boot headers.
BOARD_BOOTIMAGE_PARTITION_SIZE := 100663296
BOARD_DTBOIMG_PARTITION_SIZE := 25165824
BOARD_INIT_BOOT_IMAGE_PARTITION_SIZE := 8388608
BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := 100663296
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 104857600
BOARD_FLASH_BLOCK_SIZE := 262144

# The donor's super size does not match the measured stock rawprogram. Recovery
# does not build a super image, so no unverified dynamic-group size is declared.
BOARD_USES_METADATA_PARTITION := true

TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/recovery/root/system/etc/recovery.fstab
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
BOARD_HAS_LARGE_FILESYSTEM := true

BOARD_AVB_ENABLE := true
TW_V_AB_BOARD := true
TW_INCLUDE_FASTBOOTD := true
TW_INCLUDE_UPDATE_ENGINE := true
TW_INCLUDE_UPDATE_ENGINE_SIDELOAD := true
TW_INCLUDE_REPACKTOOLS := true
TW_INCLUDE_LPDUMP := true
TW_INCLUDE_LPTOOLS := true
TARGET_RECOVERY_DEVICE_MODULES += bootctl fsck.f2fs fsck.exfat dump.exfat mkfs.exfat ntfsresize wimlib-imagex dropbear
TW_INCLUDE_NTFS_3G := true
TW_RECOVERY_ADDITIONAL_RELINK_BINARY_FILES += \
    $(TARGET_OUT_EXECUTABLES)/ntfsresize \
    $(TARGET_OUT_EXECUTABLES)/fsck.exfat \
    $(TARGET_OUT_EXECUTABLES)/dump.exfat \
    $(TARGET_OUT_EXECUTABLES)/mkfs.exfat \
    $(TARGET_OUT_EXECUTABLES)/wimlib-imagex \
    $(TARGET_OUT_EXECUTABLES)/dropbear
TW_RECOVERY_ADDITIONAL_RELINK_LIBRARY_FILES += \
    $(TARGET_OUT_SHARED_LIBRARIES)/libminuitwrp.so \
    $(TARGET_OUT_SHARED_LIBRARIES)/libsnapshot.so \
    $(TARGET_OUT_SHARED_LIBRARIES)/libfs_mgr_binder.so
TARGET_USES_LOGD := true
TWRP_INCLUDE_LOGCAT := true

# Crypto flags disable decryption only. The shared recovery write policy also
# blocks legacy formatting, restore, flash, OTA and fastbootd mutations. Neither
# a writable mount preference nor advanced mode grants an unreviewed live write.
TW_INCLUDE_CRYPTO := false
TW_INCLUDE_CRYPTO_FBE := false
TW_INCLUDE_FBE_METADATA_DECRYPT := false

TW_THEME := portrait_hdpi
TW_ROTATION := 270
TW_FRAMERATE := 120
TW_BRIGHTNESS_PATH := /sys/class/backlight/panel0-backlight/brightness
TW_MAX_BRIGHTNESS := 2047
TW_DEFAULT_BRIGHTNESS := 200
TW_EXTRA_LANGUAGES := true
TW_DEFAULT_LANGUAGE := en
TW_EXCLUDE_DEFAULT_USB_INIT := true
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy
TW_INPUT_BLACKLIST := hbtp_vm
TW_UKE_BOUNDED_TELEMETRY := true
TW_UKE_RTC_OFFSET := true
TW_UKE_NATIVE_THEME := true
