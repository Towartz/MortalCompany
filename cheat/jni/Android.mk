# Capture this makefile's directory FIRST, before any sub-include changes LOCAL_PATH
MY_JNI_PATH := $(call my-dir)

# ── ShadowHook static library ──────────────────────────────────────────────────────
LOCAL_PATH := $(MY_JNI_PATH)
include $(LOCAL_PATH)/shadowhook/Android.mk

# ── Cheat (fbk) shared library ───────────────────────────────────────────────
include $(CLEAR_VARS)
LOCAL_PATH   := $(MY_JNI_PATH)
LOCAL_MODULE := fbk

LOCAL_SRC_FILES := \
    main.cpp \
    cheats.cpp \
    config.cpp \
    esp.cpp \
    imgui/imgui-master/imgui.cpp \
    imgui/imgui-master/imgui_draw.cpp \
    imgui/imgui-master/imgui_widgets.cpp \
    imgui/imgui-master/imgui_tables.cpp \
    imgui/imgui-master/backends/imgui_impl_opengl3.cpp \
    imgui/imgui-master/backends/imgui_impl_android.cpp \
    KittyMemory/KittyMemory/KittyMemory.cpp \
    KittyMemory/KittyMemory/KittyScanner.cpp \
    KittyMemory/KittyMemory/KittyUtils.cpp \
    KittyMemory/KittyMemory/MemoryBackup.cpp \
    KittyMemory/KittyMemory/MemoryPatch.cpp

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/imgui/imgui-master \
    $(LOCAL_PATH)/imgui/imgui-master/backends \
    $(LOCAL_PATH)/../../il2cpp_src/Il2cpp_Resolver_Android \
    $(LOCAL_PATH)/KittyMemory/KittyMemory

LOCAL_CPPFLAGS += -std=c++17 -fvisibility=hidden -DIMGUI_IMPL_OPENGL_LOADER_CUSTOM
LOCAL_LDLIBS   := -llog -lEGL -lGLESv3 -landroid

# Link Dobby as a static library (dobby.h pulled in via LOCAL_EXPORT_C_INCLUDES)
LOCAL_STATIC_LIBRARIES := shadowhook

include $(BUILD_SHARED_LIBRARY)
