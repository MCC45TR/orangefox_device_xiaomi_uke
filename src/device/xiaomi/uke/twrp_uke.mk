# SPDX-License-Identifier: Apache-2.0

$(call inherit-product, device/xiaomi/uke/device.mk)

PRODUCT_RELEASE_NAME := uke
PRODUCT_DEVICE := uke
PRODUCT_NAME := twrp_uke
PRODUCT_BRAND := Xiaomi
# Commercial display identity; the compatibility target remains exactly uke.
# Neither this shared name nor ro.soc.* authorizes model-specific storage writes.
PRODUCT_MODEL := Xiaomi Pad 7 / POCO Pad X1
PRODUCT_MANUFACTURER := Xiaomi
TARGET_OTA_ASSERT_DEVICE := uke
