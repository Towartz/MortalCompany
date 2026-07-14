LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := shadowhook

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
  SH_ARCH := arm64
else
  SH_ARCH := arm
endif

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/shadowhook/src/main/cpp \
    $(LOCAL_PATH)/shadowhook/src/main/cpp/include \
    $(LOCAL_PATH)/shadowhook/src/main/cpp/arch/$(SH_ARCH) \
    $(LOCAL_PATH)/shadowhook/src/main/cpp/common \
    $(LOCAL_PATH)/shadowhook/src/main/cpp/third_party/xdl \
    $(LOCAL_PATH)/shadowhook/src/main/cpp/third_party/bsd \
    $(LOCAL_PATH)/shadowhook/src/main/cpp/third_party/lss

# Find all C/S files using wildcard
SHADOWHOOK_SRC_DIR := shadowhook/src/main/cpp
LOCAL_SRC_FILES := \
    $(wildcard $(LOCAL_PATH)/$(SHADOWHOOK_SRC_DIR)/*.c) \
    $(wildcard $(LOCAL_PATH)/$(SHADOWHOOK_SRC_DIR)/arch/$(SH_ARCH)/*.c) \
    $(wildcard $(LOCAL_PATH)/$(SHADOWHOOK_SRC_DIR)/arch/$(SH_ARCH)/*.S) \
    $(wildcard $(LOCAL_PATH)/$(SHADOWHOOK_SRC_DIR)/common/*.c) \
    $(wildcard $(LOCAL_PATH)/$(SHADOWHOOK_SRC_DIR)/third_party/xdl/*.c)

# Strip $(LOCAL_PATH)/ from absolute paths
LOCAL_SRC_FILES := $(LOCAL_SRC_FILES:$(LOCAL_PATH)/%=%)

LOCAL_CFLAGS := -std=c11 -Os -ffunction-sections -fdata-sections -fvisibility=hidden

# Export include dir
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/shadowhook/src/main/cpp/include

include $(BUILD_STATIC_LIBRARY)
