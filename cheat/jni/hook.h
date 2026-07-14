#pragma once
#include <jni.h>
#include <shadowhook.h>
#include <android/log.h>
#include <stdint.h>

// ── Pointer sanity guard ──────────────────────────────────────────────────────
// Returns true only when p is in a plausible userspace executable range.
template <typename T>
static inline bool is_valid_fn_ptr(T p) {
    const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    return addr > 0x10000u && (addr >> 63) == 0u;
}

// ── Inline hook installer (Dobby backend) ─────────────────────────────────────
// Wraps DobbyHook with the same safety layers as before:
//
//   PRE-CHECK  – Verify the target starts with a non-zero instruction.
//                IL2CPP may resolve a method pointer before the native code
//                is JIT'd, leaving the slot as zeros. Hooking zeros crashes.
//
//   POST-CHECK – After DobbyHook, verify *original is non-null and points
//                into a valid userspace address (Dobby sets it to null on fail).
//
//   INIT       – Zero-initialise *original before the call so a partial
//                failure never leaves a stale dangling pointer.
//
// Dobby handles W^X / mprotect internally — no SIGILL from trampoline pool.
static inline bool hook_func(void* target, void* replacement, void** original) {
    if (!target || !replacement) return false;

    // ── PRE-CHECK: reject unresolved / zero-filled target functions ──
    // On ARM64, 0x00000000 = UDF #0 (undefined instruction). No real function
    // starts with UDF. If the target is zeros the method pointer from IL2CPP
    // hasn't been populated with code yet.
    uint32_t target_first = *reinterpret_cast<uint32_t*>(target);
    if (target_first == 0u) {
        __android_log_print(ANDROID_LOG_WARN, "FBK",
            "hook_func: target %p starts with UDF (0x00000000) — not resolved yet, skipping",
            target);
        return false;   // caller should retry later
    }

    if (original) *original = nullptr;

    // DobbyHook returns 0 on success, non-zero on failure.
    if (target) {
        void* handle = shadowhook_hook_func_addr(target, replacement, original);
        if (handle == nullptr) {
            __android_log_print(ANDROID_LOG_ERROR, "FBK", "ShadowHook failed for target %p", target);
            if (original) *original = nullptr;
            return false;
        }
        __android_log_print(ANDROID_LOG_INFO, "fbk", "Hooked 0x%p", target);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "FBK",
            "hook_func: hook failed for target %p", target);
        if (original) *original = nullptr;
        return false;
    }

    // ── POST-CHECK: verify Dobby set a valid original pointer ──
    if (original && *original) {
        if (!is_valid_fn_ptr(*original)) {
            __android_log_print(ANDROID_LOG_ERROR, "FBK",
                "hook_func: Dobby trampoline %p looks invalid for target %p",
                *original, target);
            *original = nullptr;
            return false;
        }
        return true;
    }

    // DobbyHook succeeded but original is null (no trampoline requested)
    return (original == nullptr);
}
