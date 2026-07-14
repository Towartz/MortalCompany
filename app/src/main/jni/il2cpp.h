#pragma once
#include "Resolver/IL2CPP_Resolver.hpp"
#include <android/log.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "FBK", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FBK", __VA_ARGS__)

namespace IL2CPP {
    namespace {
        inline void* GetRuntimeInvoke() {
            static void* s_fn = nullptr;
            if (!s_fn && Globals.m_GameAssembly)
                s_fn = dlsym(reinterpret_cast<void*>(Globals.m_GameAssembly), "il2cpp_runtime_invoke");
            return s_fn;
        }
    }
}

inline IL2CPP::CClass* AsClass(void* ptr) {
    return reinterpret_cast<IL2CPP::CClass*>(ptr);
}

inline int GetFieldOffset(const char* klass, const char* field) {
    return IL2CPP::Class::Utils::GetFieldOffset(klass, field);
}

inline Unity::il2cppMethodInfo* GetCachedMethod(const char* className, const char* methodName, int argc) {
    static std::unordered_map<std::string, Unity::il2cppMethodInfo*> s_cache;
    std::string key = std::string(className) + "." + methodName + ":" + std::to_string(argc);
    auto it = s_cache.find(key);
    if (it != s_cache.end()) return it->second;

    Unity::il2cppClass* klass = IL2CPP::Class::Find(className);
    if (!klass) return nullptr;

    typedef Unity::il2cppMethodInfo* (*GetMethodFromName_t)(void*, const char*, int);
    auto func = reinterpret_cast<GetMethodFromName_t>(IL2CPP::Functions.m_ClassGetMethodFromName);
    Unity::il2cppMethodInfo* method = func(klass, methodName, argc);
    if (method) s_cache[key] = method;
    return method;
}

// Invoke a managed method. Each entry in `args` is normally a POINTER to the
// argument (correct for objects/structs passed by reference). For BY-VALUE
// primitives (int/enum/float/bool), set the corresponding bit in byValueMask so
// the actual value is passed instead of its address — RuntimeInvoke reads
// primitive args directly out of the args slots (as 8-byte values on AArch64).
inline bool InvokeManaged(void* obj, const char* className, const char* methodName, int argc, void** args, unsigned long long byValueMask = 0) {
    Unity::il2cppMethodInfo* pMethod = GetCachedMethod(className, methodName, argc);
    if (!pMethod) return false;

    typedef void* (*t_invoke)(void*, void*, void**, void**);
    static t_invoke s_runtimeInvoke = nullptr;
    if (!s_runtimeInvoke) s_runtimeInvoke = reinterpret_cast<t_invoke>(IL2CPP::GetRuntimeInvoke());
    if (!s_runtimeInvoke) return false;

    // Build a marshaled arg array so we never mutate the caller's buffers.
    void* marshaled[8];
    union Slot { void* p; unsigned long long v; float f; } tmp[8];
    for (int i = 0; i < argc && i < 8; i++) {
        if (byValueMask & (1ull << i)) {
            // Copy the primitive value (max 8 bytes) into the slot as-is.
            tmp[i].v = 0;
            memcpy(&tmp[i].v, args[i], sizeof(void*) < 8 ? sizeof(void*) : 8);
            marshaled[i] = tmp[i].p;
        } else {
            marshaled[i] = args[i];
        }
    }

    void* exc = nullptr;
    s_runtimeInvoke(pMethod, obj, marshaled, &exc);
    return exc == nullptr;
}
