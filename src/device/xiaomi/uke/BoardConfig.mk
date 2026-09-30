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
TARGET_RECOVERY_DEVICE_MODULES += bootctl
TARGET_USES_LOGD := true
TWRP_INCLUDE_LOGCAT := true

# Decryption and destructive data operations stay disabled until the installed
# firmware's KeyMint/TEE path and both storage variants pass device tests.
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
TW_INPUT_BLACKLIST := hbtp_vm
