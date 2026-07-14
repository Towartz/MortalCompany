#pragma once
#include <cstdint>
#include <dlfcn.h>
#include <android/log.h>

#define CHEAT_TAG "CHEAT"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, CHEAT_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CHEAT_TAG, __VA_ARGS__)

#define IL2CPP_MAIN_MODULE "libil2cpp.so"
#include <IL2CPP_Resolver.hpp>

// Helper: cast any instance ptr to CClass* for SetMemberValue/GetMemberValue
inline IL2CPP::CClass* AsClass(void* ptr) {
    return reinterpret_cast<IL2CPP::CClass*>(ptr);
}

// Helper: get field offset from class+field name
inline int GetFieldOffset(const char* klass, const char* field) {
    return IL2CPP::Class::Utils::GetFieldOffset(klass, field);
}

// ── Cached method pointer for InvokeManaged ──
// Resolves the Unity::il2cppMethodInfo* once per (class, method, argc) combo
inline Unity::il2cppMethodInfo* GetCachedMethod(const char* className, const char* methodName, int argc) {
    // Hash the lookup key
    uint32_t h = IL2CPP::Utils::Hash::Get(className);
    h = h * 33 + IL2CPP::Utils::Hash::Get(methodName);
    h = h * 33 + static_cast<uint32_t>(argc);

    static std::unordered_map<uint32_t, Unity::il2cppMethodInfo*> s_methodCache;
    auto it = s_methodCache.find(h);
    if (it != s_methodCache.end())
        return it->second;

    Unity::il2cppClass* c = IL2CPP::Class::Find(className);
    if (!c) return nullptr;
    typedef Unity::il2cppMethodInfo* (*t_get_method)(void*, const char*, int);
    Unity::il2cppMethodInfo* m = ((t_get_method)IL2CPP::Functions.m_ClassGetMethodFromName)(c, methodName, argc);
    if (m) s_methodCache[h] = m;
    return m;
}

// Helper: invoke a managed method by cached class+method name (uses il2cpp_runtime_invoke)
// Method pointer is resolved once and cached for subsequent calls
inline bool InvokeManaged(void* obj, const char* className, const char* methodName, int argc, void** args) {
    Unity::il2cppMethodInfo* pMethod = GetCachedMethod(className, methodName, argc);
    if (!pMethod) return false;

    static void* s_runtimeInvoke = nullptr;
    if (!s_runtimeInvoke) {
        void* h = dlopen("libil2cpp.so", RTLD_NOLOAD);
        if (h) s_runtimeInvoke = dlsym(h, "il2cpp_runtime_invoke");
    }
    if (!s_runtimeInvoke) return false;

    void* exc = nullptr;
    typedef void* (*t_invoke)(void*, void*, void**, void**);
    ((t_invoke)s_runtimeInvoke)(pMethod, obj, args, &exc);
    return exc == nullptr;
}
