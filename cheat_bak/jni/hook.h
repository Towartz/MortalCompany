#pragma once
#include "And64InlineHook.hpp"
#include <android/log.h>

static inline bool hook_func(void* target, void* replacement, void** original) {
    if (!target || !replacement) return false;
    A64HookFunction(target, replacement, original);
    return *original != nullptr;
}
