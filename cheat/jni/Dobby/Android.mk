LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dobby

# ── ARM64 + Android/Linux source files (relative to Dobby dir) ───────────────
LOCAL_SRC_FILES := \
    source/dobby.cpp \
    \
    source/core/assembler/assembler-arm.cc \
    source/core/assembler/assembler-ia32.cc \
    source/core/assembler/assembler-x64.cc \
    \
    source/core/codegen/codegen-arm.cc \
    source/core/codegen/codegen-ia32.cc \
    \
    source/InstructionRelocation/arm/InstructionRelocationARM.cc \
    source/InstructionRelocation/arm64/InstructionRelocationARM64.cc \
    source/InstructionRelocation/x86/InstructionRelocationX86.cc \
    source/InstructionRelocation/x86/InstructionRelocationX86Shared.cc \
    source/InstructionRelocation/x64/InstructionRelocationX64.cc \
    source/InstructionRelocation/x86/x86_insn_decode/x86_insn_decode.c \
    \
    source/InterceptRouting/InstrumentRouting/instrument_routing_handler.cpp \
    source/InterceptRouting/NearBranchTrampoline/near_trampoline_arm64.cc \
    \
    source/TrampolineBridge/Trampoline/trampoline_arm.cc \
    source/TrampolineBridge/Trampoline/trampoline_arm64.cc \
    source/TrampolineBridge/Trampoline/trampoline_x86.cc \
    source/TrampolineBridge/Trampoline/trampoline_x64.cc \
    \
    source/TrampolineBridge/ClosureTrampolineBridge/arm64/helper_arm64.cc \
    source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.cc \
    source/TrampolineBridge/ClosureTrampolineBridge/arm64/ClosureTrampolineARM64.cc \
    source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.S \
    source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_trampoline_arm64.S \
    \
    source/TrampolineBridge/ClosureTrampolineBridge/arm/helper_arm.cc \
    source/TrampolineBridge/ClosureTrampolineBridge/arm/closure_bridge_arm.cc \
    source/TrampolineBridge/ClosureTrampolineBridge/arm/ClosureTrampolineARM.cc \
    \
    source/TrampolineBridge/ClosureTrampolineBridge/x86/helper_x86.cc \
    source/TrampolineBridge/ClosureTrampolineBridge/x86/closure_bridge_x86.cc \
    source/TrampolineBridge/ClosureTrampolineBridge/x86/ClosureTrampolineX86.cc \
    \
    source/TrampolineBridge/ClosureTrampolineBridge/x64/helper_x64.cc \
    source/TrampolineBridge/ClosureTrampolineBridge/x64/closure_bridge_x64.cc \
    source/TrampolineBridge/ClosureTrampolineBridge/x64/ClosureTrampolineX64.cc \
    source/TrampolineBridge/ClosureTrampolineBridge/x64/closure_bridge_x64.S \
    source/TrampolineBridge/ClosureTrampolineBridge/x64/closure_trampoline_x64.S \
    \
    source/Backend/UserMode/PlatformUtil/Linux/ProcessRuntime.cc \
    source/Backend/UserMode/UnifiedInterface/platform-posix.cc \
    source/Backend/UserMode/ExecMemory/code-patch-tool-posix.cc \
    source/Backend/UserMode/ExecMemory/clear-cache-tool-all.c \
    \
    external/logging/logging.cc

# ── Include paths ────────────────────────────────────────────────────────────
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/include \
    $(LOCAL_PATH)/source \
    $(LOCAL_PATH)/source/dobby \
    $(LOCAL_PATH)/common \
    $(LOCAL_PATH)/external \
    $(LOCAL_PATH)/external/logging \
    $(LOCAL_PATH)/external/TINYSTL \
    $(LOCAL_PATH)/builtin-plugin \
    $(LOCAL_PATH)/source/Backend/UserMode \
    $(LOCAL_PATH)/source/PlatformUtil \
    $(LOCAL_PATH)/source/PlatformUnifiedInterface

# ── Export dobby.h to modules that depend on this ────────────────────────────
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include

# ── Compiler flags ────────────────────────────────────────────────────────────
LOCAL_CPPFLAGS := \
    -std=c++17 \
    -fvisibility=hidden \
    -DDOBBY_LOGGING_DISABLE \
    -DBUILD_WITH_TRAMPOLINE_ASM \
    -D__DOBBY_BUILD_VERSION__=\"DobbyAndroid\"

LOCAL_CFLAGS := \
    -fvisibility=hidden \
    -DDOBBY_LOGGING_DISABLE \
    -DBUILD_WITH_TRAMPOLINE_ASM

include $(BUILD_STATIC_LIBRARY)
