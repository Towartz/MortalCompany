LOCAL_PATH := $(call my-dir)

# ---------------- Cheat module ----------------
include $(CLEAR_VARS)
LOCAL_MODULE := fbk
LOCAL_SRC_FILES := \
    main.cpp \
    cheats.cpp \
    config.cpp \
    esp.cpp \
    And64InlineHook/And64InlineHook.cpp \
    imgui/imgui-master/imgui.cpp \
    imgui/imgui-master/imgui_draw.cpp \
    imgui/imgui-master/imgui_widgets.cpp \
    imgui/imgui-master/imgui_tables.cpp \
    imgui/imgui-master/backends/imgui_impl_opengl3.cpp \
    imgui/imgui-master/backends/imgui_impl_android.cpp

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/And64InlineHook \
    $(LOCAL_PATH)/imgui/imgui-master \
    $(LOCAL_PATH)/imgui/imgui-master/backends \
    $(LOCAL_PATH)/../../il2cpp_src/Il2cpp_Resolver_Android

LOCAL_CPPFLAGS += -std=c++17 -fvisibility=hidden -DIMGUI_IMPL_OPENGL_LOADER_CUSTOM
LOCAL_LDLIBS := -llog -lEGL -lGLESv3 -landroid
include $(BUILD_SHARED_LIBRARY)
