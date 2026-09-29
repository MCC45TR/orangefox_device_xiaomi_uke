# SPDX-License-Identifier: Apache-2.0
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := uke-recoveryctl
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
LOCAL_SRC_FILES := recoveryctl.cpp
LOCAL_CPPFLAGS := -std=c++20 -fexceptions -Wall -Wextra -Werror
include $(BUILD_EXECUTABLE)
