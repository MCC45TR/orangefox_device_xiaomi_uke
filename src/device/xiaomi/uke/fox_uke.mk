# SPDX-License-Identifier: GPL-3.0-or-later

OF_SCREEN_H := 3200
OF_STATUS_H := 115
OF_HIDE_NOTCH := 1
OF_CLOCK_POS := 1
OF_STATUS_INDENT_LEFT := 56
OF_STATUS_INDENT_RIGHT := 48
OF_ALLOW_DISABLE_NAVBAR := 0

OF_AB_DEVICE_WITH_RECOVERY_PARTITION := 1
OF_ENABLE_LPTOOLS := 1
OF_ENABLE_ALL_PARTITION_TOOLS := 1
OF_USE_LZ4_COMPRESSION := 1
OF_FORCE_PREBUILT_KERNEL := 1
OF_USE_AIDL_BOOT_CONTROL := 1
OF_DISABLE_ORS_AUTO_REBOOT := 1
OF_LOOP_DEVICE_ERRORS_TO_LOG := 1
OF_USE_LOCKSCREEN_BUTTON := 1

# OrangeFox's default flashlight path targets phone torch LEDs. Neither a
# camera flash nor a matching Uke recovery sysfs path is verified; hide the
# control until per-model hardware evidence supplies a safe device path.
OF_FLASHLIGHT_ENABLE := 0

# FBE access remains off for the first build profile. It will be enabled only
# after firmware-matched KeyMint/TEE evidence and a read-only device test.
OF_SKIP_FBE_DECRYPTION := 1
OF_MAINTAINER := MCC45TR
