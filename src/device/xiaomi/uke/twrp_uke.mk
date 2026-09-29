# SPDX-License-Identifier: Apache-2.0

$(call inherit-product, device/xiaomi/uke/device.mk)

PRODUCT_RELEASE_NAME := uke
PRODUCT_DEVICE := uke
PRODUCT_NAME := twrp_uke
PRODUCT_BRAND := Xiaomi
# Both commercial models use the Uke recovery target; release metadata records
# the exact validated model and firmware profile.
PRODUCT_MODEL := Uke Recovery
PRODUCT_MANUFACTURER := Xiaomi
TARGET_OTA_ASSERT_DEVICE := uke
