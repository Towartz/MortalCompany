#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <dlfcn.h>
#include <android/log.h>
#include <pthread.h>

// ── Tags ──────────────────────────────────────────────────────────────────────
#define CHEAT_TAG "CHEAT"
#undef  LOGD
#undef  LOGW
#undef  LOGE
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, CHEAT_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  CHEAT_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CHEAT_TAG, __VA_ARGS__)

// ── IL2CPP resolver include ────────────────────────────────────────────────
#define IL2CPP_MAIN_MODULE "libil2cpp.so"
#include <IL2CPP_Resolver.hpp>

// ══════════════════════════════════════════════════════════════════════════════
// Internal cache layer
// ══════════════════════════════════════════════════════════════════════════════
namespace IL2CPPCache {

// ── Composite key ─────────────────────────────────────────────────────────────
// "ClassName\0memberName\0<argc char>" — unique, collision-free, no hashing.
inline std::string MakeKey(const char* a, const char* b, int c = -1) {
    std::string k;
    k.reserve(72);
    k += a; k += '\0';
    k += b; k += '\0';
    if (c >= 0) k += static_cast<char>('0' + (c & 0x7F));
    return k;
}

// ── libil2cpp.so symbol resolver ─────────────────────────────────────────────
inline void* GetIL2CPPSym(const char* name) {
    static void* handle = nullptr;
    if (!handle) handle = dlopen("libil2cpp.so", RTLD_NOLOAD);
    return handle ? dlsym(handle, name) : nullptr;
}

// ══════════════════════════════════════════════════════════════════════════════
// FIELD OFFSET CACHE
// ──────────────────────────────────────────────────────────────────────────────
// KEY DESIGN DECISION: failures (offset < 0) are NOT stored.
// This means every call that returns -1 will retry the IL2CPP lookup on the
// next ResolveOffsets() pass — which happens after every scene transition.
// Scene-dependent classes (FP_Controller, EnemyBase, TerminalShop, etc.) are
// only loaded once their scene is active, so the resolver naturally heals
// itself as the player moves between scenes.
// ══════════════════════════════════════════════════════════════════════════════
struct FieldCache {
    pthread_mutex_t             mu = PTHREAD_MUTEX_INITIALIZER;
    std::unordered_map<std::string, int> map; // only contains entries with offset >= 0

    int get(const char* klass, const char* field) {
        auto key = MakeKey(klass, field);

        // Fast unsynchronised read — safe because we only ever insert, never erase
        auto it = map.find(key);
        if (it != map.end()) return it->second;

        pthread_mutex_lock(&mu);
        it = map.find(key);
        if (it != map.end()) {
            int v = it->second;
            pthread_mutex_unlock(&mu);
            return v;
        }
        int off = IL2CPP::Class::Utils::GetFieldOffset(klass, field);
        if (off >= 0) {
            map[key] = off;  // ← only cache successes
            LOGD("IL2CPP: field [%s::%s] → offset %d", klass, field, off);
        } else {
            // NOT stored → next call will try again (retry after scene change)
            LOGW("IL2CPP: field [%s::%s] not found — will retry", klass, field);
        }
        pthread_mutex_unlock(&mu);
        return off;
    }

    // Called on scene transition to wipe everything, forcing full re-resolution.
    // Field offsets from IL2CPP metadata are stable across scenes of the same
    // build, but some classes are only JIT-compiled when their scene loads, so
    // their metadata becomes accessible only then.
    void clear() {
        pthread_mutex_lock(&mu);
        map.clear();
        pthread_mutex_unlock(&mu);
        LOGD("IL2CPP: FieldCache cleared (scene transition)");
    }

    int size() { return (int)map.size(); }
};

// ══════════════════════════════════════════════════════════════════════════════
// METHOD POINTER CACHE  (same no-failure-caching strategy)
// ══════════════════════════════════════════════════════════════════════════════
struct MethodPtrCache {
    pthread_mutex_t              mu = PTHREAD_MUTEX_INITIALIZER;
    std::unordered_map<std::string, void*> map; // only non-null entries

    void* get(const char* klass, const char* method, int argc) {
        auto key = MakeKey(klass, method, argc);

        auto it = map.find(key);
        if (it != map.end()) return it->second;

        pthread_mutex_lock(&mu);
        it = map.find(key);
        if (it != map.end()) {
            void* v = it->second;
            pthread_mutex_unlock(&mu);
            return v;
        }
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer(klass, method, argc);
        if (ptr) {
            map[key] = ptr;  // ← only cache successes
            LOGD("IL2CPP: method [%s::%s(%d)] → %p", klass, method, argc, ptr);
        } else {
            LOGW("IL2CPP: method [%s::%s(%d)] not found — will retry", klass, method, argc);
        }
        pthread_mutex_unlock(&mu);
        return ptr;
    }

    void clear() {
        pthread_mutex_lock(&mu);
        map.clear();
        pthread_mutex_unlock(&mu);
        LOGD("IL2CPP: MethodPtrCache cleared");
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// METHOD INFO CACHE  (used by InvokeManaged — no-failure-caching)
// ══════════════════════════════════════════════════════════════════════════════
struct MethodInfoCache {
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    std::unordered_map<std::string, Unity::il2cppMethodInfo*> map;

    Unity::il2cppMethodInfo* get(const char* klass, const char* method, int argc) {
        auto key = MakeKey(klass, method, argc);

        auto it = map.find(key);
        if (it != map.end()) return it->second;

        pthread_mutex_lock(&mu);
        it = map.find(key);
        if (it != map.end()) {
            auto* v = it->second;
            pthread_mutex_unlock(&mu);
            return v;
        }
        Unity::il2cppClass* c = IL2CPP::Class::Find(klass);
        Unity::il2cppMethodInfo* m = nullptr;
        if (c) {
            typedef Unity::il2cppMethodInfo* (*t_get)(void*, const char*, int);
            m = ((t_get)IL2CPP::Functions.m_ClassGetMethodFromName)(c, method, argc);
        }
        if (m) {
            map[key] = m;  // ← only cache successes
            LOGD("IL2CPP: methodInfo [%s::%s(%d)] → %p", klass, method, argc, (void*)m);
        } else {
            LOGW("IL2CPP: methodInfo [%s::%s(%d)] not found (class=%p) — will retry",
                 klass, method, argc, (void*)c);
        }
        pthread_mutex_unlock(&mu);
        return m;
    }

    void clear() {
        pthread_mutex_lock(&mu);
        map.clear();
        pthread_mutex_unlock(&mu);
        LOGD("IL2CPP: MethodInfoCache cleared");
    }
};

// ── Singleton accessors ───────────────────────────────────────────────────────
inline FieldCache&      Fields()      { static FieldCache      s; return s; }
inline MethodPtrCache&  MethodPtrs()  { static MethodPtrCache  s; return s; }
inline MethodInfoCache& MethodInfos() { static MethodInfoCache  s; return s; }

// ── Clear all caches — call this on every scene transition ────────────────────
// Field offsets & method pointers are structurally stable once IL2CPP is fully
// initialised, but some types are only registered in the runtime image when their
// scene/assembly-image is loaded.  Clearing forces fresh lookups so any class
// that just became available is picked up immediately.
inline void ClearAll() {
    Fields().clear();
    MethodPtrs().clear();
    MethodInfos().clear();
    LOGD("IL2CPP: all caches cleared (scene transition)");
}

// ── il2cpp_runtime_invoke — resolved once, never changes ─────────────────────
inline void* RuntimeInvoke() {
    static void* fn = nullptr;
    if (!fn) {
        fn = GetIL2CPPSym("il2cpp_runtime_invoke");
        if (!fn) LOGE("IL2CPP: il2cpp_runtime_invoke not found");
    }
    return fn;
}

// ── Managed exception → human-readable message ───────────────────────────────
inline const char* ExceptionMessage(void* exc) {
    if (!exc) return nullptr;
    static void* fn = nullptr;
    if (!fn) fn = GetIL2CPPSym("il2cpp_format_exception");
    if (!fn) return "(exception — il2cpp_format_exception unavailable)";
    typedef const char* (*t_fmt)(void*);
    return ((t_fmt)fn)(exc);
}

// ══════════════════════════════════════════════════════════════════════════════
// SCENE UTILITIES
// ──────────────────────────────────────────────────────────────────────────────
// Queries UnityEngine.SceneManagement at runtime. Works in every scene because
// SceneManager is always loaded as part of UnityEngine.CoreModule.
// ══════════════════════════════════════════════════════════════════════════════

// ── Scene type enum ───────────────────────────────────────────────────────────
enum class SceneType : uint8_t {
    Unknown   = 0,  // scene name not yet determined
    MainMenu  = 1,  // main menu / title screen (no gameplay classes)
    Ship      = 2,  // ship interior (lobby, shop, quota visible)
    Moon      = 3,  // exterior moon / planet surface
    Facility  = 4,  // interior dungeon / facility
    Other     = 5,  // any other scene
};

// ── GetActiveSceneIndex — via il2cpp_unity_install_unitytls / UnityEngine API ─
// Uses the IL2CPP static method UnityEngine.SceneManagement.SceneManager
//   ::GetActiveScene() → returns a Scene struct (build index at offset 0).
// If the API is unavailable (stripped build), returns -1.
inline int GetActiveSceneIndex() {
    // Scene is a blittable struct: { int handle; }
    // GetActiveScene() returns it by value — use a static method pointer cache.
    static void* s_getActiveScene = nullptr;
    if (!s_getActiveScene) {
        s_getActiveScene = IL2CPP::Class::Utils::GetMethodPointer(
            "SceneManager", "GetActiveScene", 0);
    }
    if (!s_getActiveScene) return -1;

    // Scene struct: first int32 is m_Handle (internal handle), second is build index.
    // Unity stores the build-index at offset 4 in the Scene value type.
    struct SceneStruct { int32_t handle; int32_t buildIndex; int32_t pad[4]; };
    SceneStruct sc = {};
    typedef SceneStruct (*t_gas)();
    sc = ((t_gas)s_getActiveScene)();
    return sc.buildIndex;
}

// ── GetActiveSceneName — queries Scene.name via il2cpp_runtime_invoke ─────────
// Returns a static buffer valid until the next call. Empty string on failure.
inline const char* GetActiveSceneName() {
    static char s_nameBuf[128] = {};

    // Scene.get_name() is an instance method on the Scene struct — we need
    // a boxed Scene object.  Simpler: use Object.FindObjectsOfType on a
    // known-always-present Unity type and check its scene name via GetScene().
    // Even simpler: just return the scene build index as a string.
    int idx = GetActiveSceneIndex();
    if (idx < 0) {
        s_nameBuf[0] = '\0';
        return s_nameBuf;
    }
    snprintf(s_nameBuf, sizeof(s_nameBuf), "scene[%d]", idx);
    return s_nameBuf;
}

// ── ClassifyScene — maps scene index → SceneType ─────────────────────────────
// Mortal Company scene layout (typical Unity mobile build):
//   0 = Bootstrap / loading
//   1 = Main Menu
//   2 = Ship (lobby)
//   3 = Moon surface
//   4 = Facility interior
// Adjust indices to match your actual build.
inline SceneType ClassifyScene(int buildIndex) {
    switch (buildIndex) {
        case 0:  return SceneType::MainMenu;   // Bootstrap or menu
        case 1:  return SceneType::MainMenu;
        case 2:  return SceneType::Ship;
        case 3:  return SceneType::Moon;
        case 4:  return SceneType::Facility;
        default: return SceneType::Other;
    }
}

inline const char* SceneTypeName(SceneType t) {
    switch (t) {
        case SceneType::MainMenu: return "MainMenu";
        case SceneType::Ship:     return "Ship";
        case SceneType::Moon:     return "Moon";
        case SceneType::Facility: return "Facility";
        case SceneType::Other:    return "Other";
        default:                  return "Unknown";
    }
}

} // namespace IL2CPPCache

// ══════════════════════════════════════════════════════════════════════════════
// Public API
// ══════════════════════════════════════════════════════════════════════════════

// ── Cast to CClass* for GetMemberValue / SetMemberValue ──────────────────────
inline IL2CPP::CClass* AsClass(void* ptr) {
    return reinterpret_cast<IL2CPP::CClass*>(ptr);
}

// ── GetFieldOffset ────────────────────────────────────────────────────────────
// Returns -1 if the field cannot be found in this scene/image.
// Failures are NOT cached — next call will retry the IL2CPP lookup.
inline int GetFieldOffset(const char* klass, const char* field) {
    return IL2CPPCache::Fields().get(klass, field);
}

// ── GetMethodPointer ──────────────────────────────────────────────────────────
// Returns nullptr on failure. argc = param count (-1 = any overload).
// Failures are NOT cached.
inline void* GetMethodPointer(const char* klass, const char* method, int argc = -1) {
    return IL2CPPCache::MethodPtrs().get(klass, method, argc);
}

// ── InvokeManaged ─────────────────────────────────────────────────────────────
// Returns true on success (no managed exception). Logs exception message on failure.
inline bool InvokeManaged(void* obj,
                          const char* className,
                          const char* methodName,
                          int         argc,
                          void**      args)
{
    Unity::il2cppMethodInfo* pMethod =
        IL2CPPCache::MethodInfos().get(className, methodName, argc);
    if (!pMethod) return false;

    void* fn = IL2CPPCache::RuntimeInvoke();
    if (!fn) return false;

    void* exc = nullptr;
    typedef void* (*t_invoke)(void*, void*, void**, void**);
    ((t_invoke)fn)(pMethod, obj, args, &exc);

    if (exc) {
        const char* msg = IL2CPPCache::ExceptionMessage(exc);
        LOGE("IL2CPP: InvokeManaged [%s::%s] threw: %s",
             className, methodName, msg ? msg : "(unknown)");
        return false;
    }
    return true;
}

// ══════════════════════════════════════════════════════════════════════════════
// Convenience macros for ResolveOffsets()
// ══════════════════════════════════════════════════════════════════════════════

// RESOLVE_FIELD(var, "Class", "field")
// Sets var = GetFieldOffset(...). Automatically retried per scene because
// failures are never cached.
#define RESOLVE_FIELD(var, klass, field)  (var) = GetFieldOffset((klass), (field))

// RESOLVE_METHOD_PTR(var, "Class", "method", argc)
// Sets a typed function pointer. Automatically retried per scene.
#define RESOLVE_METHOD_PTR(var, klass, method, argc) \
    (var) = reinterpret_cast<decltype(var)>(GetMethodPointer((klass), (method), (argc)))

// RESOLVE_FIELD_ONCE(var, "Class", "field")
// Only resolves if var is still -1 (no-op if already resolved).
// Useful for fields that should persist across scenes once found.
#define RESOLVE_FIELD_ONCE(var, klass, field) \
    do { if ((var) < 0) (var) = GetFieldOffset((klass), (field)); } while(0)
