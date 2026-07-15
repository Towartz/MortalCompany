#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <GLES3/gl3.h>
#include <chrono>

#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Includes/Macros.h"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"
#include "dobby.h"

#include "config.h"
#include "il2cpp.h"
#include "cheats.h"
#include "esp_data.h"
#include "esp.h"

#define targetLibName OBFUSCATE("libil2cpp.so")

// Atomic flag: set only after ALL Dobby hooks are installed.
// Hooks early-return without custom logic until this flag is true,
// preventing the race between hook installation and first invocation.
static std::atomic<bool> g_hooksReady{false};

// ── Feature list for Java overlay ──
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    const char *features[] = {
        OBFUSCATE("0_Category_Movement"),
        OBFUSCATE("1_Toggle_Infinite Stamina"),
        OBFUSCATE("2_Toggle_Unlimited Sprint"),
        OBFUSCATE("3_Toggle_Speed Hack"),
        OBFUSCATE("4_Toggle_Super Jump"),
        OBFUSCATE("5_Toggle_Infinity Jumps"),
        OBFUSCATE("6_Toggle_No Gravity"),
        OBFUSCATE("7_Toggle_Air Control"),
        OBFUSCATE("8_Toggle_No Fall Damage"),

        OBFUSCATE("9_Category_Player"),
        OBFUSCATE("10_Toggle_God Mode"),
        OBFUSCATE("11_Toggle_Stamina Manipulation"),

        OBFUSCATE("12_Category_Items"),
        OBFUSCATE("13_Toggle_Unlimited Flashlight"),
        OBFUSCATE("14_Toggle_Unlimited Usage"),
        OBFUSCATE("15_Toggle_Infinite Jetpack Fuel"),
        OBFUSCATE("16_Toggle_No Weight Limit"),
        OBFUSCATE("17_Toggle_Super Item Magnet"),
        OBFUSCATE("18_Toggle_Auto Pickup"),
        OBFUSCATE("19_Toggle_Set Scrap Value"),
        OBFUSCATE("20_Button_Bypass Ad Reward"),
        OBFUSCATE("21_Button_Switch To Online"),

        OBFUSCATE("22_Category_Combat"),
        OBFUSCATE("23_Toggle_One-Hit Kill"),
        OBFUSCATE("24_Toggle_Instant Kill All"),
        OBFUSCATE("25_Toggle_Blind Enemies"),

        OBFUSCATE("26_Category_Economy"),
        OBFUSCATE("27_Toggle_Unlimited Money"),
        OBFUSCATE("28_Toggle_Free Items"),
        OBFUSCATE("29_Toggle_Quota Manipulation"),

        OBFUSCATE("30_Category_Traps & Doors"),
        OBFUSCATE("31_Toggle_Instant Doors"),
        OBFUSCATE("32_Toggle_Disable Mines"),
        OBFUSCATE("33_Toggle_Ship Door Always Open"),
        OBFUSCATE("34_Toggle_No Teleporter Cooldown"),
        OBFUSCATE("35_Button_Teleport to Ship"),
        OBFUSCATE("36_Toggle_Block Ads"),

        OBFUSCATE("37_Category_ESP"),
        OBFUSCATE("38_Toggle_ESP Players"),
        OBFUSCATE("39_Toggle_ESP Objects"),
        OBFUSCATE("40_Toggle_ESP Boxes"),
        OBFUSCATE("41_Toggle_ESP Tracelines"),
        OBFUSCATE("42_Toggle_ESP Labels"),
        OBFUSCATE("60_Toggle_ESP Valuables Only"),
        OBFUSCATE("61_Toggle_ESP Show Names"),
        OBFUSCATE("43_SeekBar_ESP Max Dist_5_100"),

        OBFUSCATE("44_Category_Visual"),
        OBFUSCATE("45_Toggle_Full Bright"),

        OBFUSCATE("46_Category_Lobby"),
        OBFUSCATE("47_Toggle_Anti-Kick"),
        OBFUSCATE("48_Button_Kick All Players"),
        OBFUSCATE("49_Toggle_Anti-Ban"),
        OBFUSCATE("50_Toggle_Clear My Kick List"),
        OBFUSCATE("51_Button_Force Spectator Others"),

        OBFUSCATE("52_Category_Grief"),
        OBFUSCATE("53_Button_Teleport Players to Ship"),
        OBFUSCATE("54_Button_Kill All Players"),
        OBFUSCATE("55_Button_Open All Doors"),
        OBFUSCATE("56_Button_Trigger Traps"),
        OBFUSCATE("57_Button_Spawn Enemies"),
        OBFUSCATE("58_Button_Boombox Spam"),
        OBFUSCATE("59_Button_Shuffle Items"),
        OBFUSCATE("62_Button_Spawn Items"),
    };

    int Total_Feature = sizeof features / sizeof features[0];
    ret = (jobjectArray) env->NewObjectArray(
        Total_Feature, env->FindClass(OBFUSCATE("java/lang/String")),
        env->NewStringUTF(""));

    for (int i = 0; i < Total_Feature; i++)
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));

    return ret;
}

// ── JNI callback from Java overlay when user toggles a feature ──
void Changes(JNIEnv *env, jclass clazz, jobject obj,
             jint featNum, jstring featName, jint value,
             jlong Lvalue, jboolean boolean, jstring text) {

    if (featNum >= 0)
        LOGI("Changes: featNum=%d boolean=%d", (int)featNum, (int)boolean);

    switch (featNum) {
        case 1:  g_config.infiniteStamina = boolean; break;
        case 2:  g_config.unlimitedSprint = boolean; break;
        case 3:  g_config.speedHack = boolean; break;
        case 4:  g_config.superJump = boolean; break;
        case 5:  g_config.infinityJumps = boolean; break;
        case 6:  g_config.noGravity = boolean; break;
        case 7:  g_config.airControl = boolean; break;
        case 8:  g_config.noFallDamage = boolean; break;

        case 10: g_config.godMode = boolean; break;
        case 11: g_config.staminaManipulation = boolean; break;

        case 13: g_config.unlimitedFlashlight = boolean; break;
        case 14: g_config.unlimitedUsage = boolean; break;
        case 15: g_config.infiniteJetpackFuel = boolean; break;
        case 16: g_config.noWeightLimit = boolean; break;
        case 17: g_config.superItemMagnet = boolean; break;
        case 18: g_config.autoPickup = boolean; break;
        case 19: g_config.setScrapValue = boolean; break;

        case 20: g_requestBypassReward = true; break;   // Bypass Ad Reward (oneshot, run on Unity thread)
        case 21: g_requestSwitchOnline = true; break;   // Switch To Online (oneshot, run on Unity thread)

        case 23: g_config.oneHitKill = boolean; break;
        case 24: g_config.instantKillAll = boolean; break;
        case 25: g_config.blindEnemies = boolean; break;

        case 27: g_config.unlimitedMoney = boolean; break;
        case 28: g_config.freeItems = boolean; break;
        case 29: g_config.quotaManipulation = boolean; break;

        case 31: g_config.instantDoors = boolean; break;
        case 32: g_config.disableMines = boolean; break;
        case 33: g_config.shipDoorAlwaysOpen = boolean; break;
        case 34: g_config.noTeleporterCooldown = boolean; break;
        case 35: g_config.instantTeleport = true; break;
        case 36: g_config.blockAds = boolean; break;

        case 38: g_config.espPlayers = boolean; break;
        case 39: g_config.espObjects = boolean; break;
        case 40: g_config.espBoxes = boolean; break;
        case 41: g_config.espTracelines = boolean; break;
        case 42: g_config.espLabels = boolean; break;
        case 60: g_config.espValuablesOnly = boolean; break;
        case 61: g_config.espShowNames = boolean; break;
        case 43: g_config.espMaxDist = (float)value; break;
        case 45: g_config.fullBright = boolean; break;

        case 47: g_config.antiKick = boolean; break;
        case 48: g_config.kickAllPlayers = true; break;   // Kick All Players (oneshot, run on Unity thread)
        case 49: g_config.antiBan = boolean; break;
        case 50: g_config.clearKickList = boolean; break;
        case 51: g_config.forceSpectatorAll = true; break; // Force Spectator Others (oneshot)
        case 53: g_config.griefTeleportShip = true; break; // Teleport Players to Ship (oneshot)
        case 54: g_config.griefKill = true; break;         // Kill All Players (oneshot)
        case 55: g_config.griefOpenDoors = true; break;    // Open All Doors (oneshot)
        case 56: g_config.griefTraps = true; break;        // Trigger Traps (oneshot)
        case 57: g_config.griefSpawnEnemies = true; break; // Spawn Enemies (oneshot)
        case 58: g_config.griefBoombox = true; break;      // Boombox Spam (oneshot)
        case 59: g_config.griefShuffleItems = true; break; // Shuffle Items (oneshot)
        case 62: g_config.spawnItems = true; break;        // Spawn Items (oneshot)

        default: break;
    }
}

// ── FP_Controller::Update hook — main cheat loop ──
typedef void (*FP_Update_t)(void*);
static FP_Update_t orig_FP_Update = nullptr;

static int g_cheatFrame = 0;

static int g_fpCallCount = 0;

static void my_FP_Update(void* instance) {
    if (orig_FP_Update) orig_FP_Update(instance);

    if (++g_cheatFrame >= 5) {
        g_cheatFrame = 0;
        if (++g_fpCallCount <= 3 || g_fpCallCount % 100 == 0)
            LOGD("FP_Update #%d", g_fpCallCount);
        CRASH_GUARD(ApplyCheats());
    }

    if (g_config.espPlayers || g_config.espObjects) {
        CRASH_GUARD(TriggerESPRefresh());
        CRASH_GUARD(GatherESPData());
    }
}

// ── RewardedAdsForOnline::OnOnlineModeButtonPressed hook ──
typedef void (*OnOnlineModeButtonPressed_t)(void*);
static OnOnlineModeButtonPressed_t orig_OnOnlineModeButtonPressed = nullptr;

static void my_OnOnlineModeButtonPressed(void* instance) {
    if (g_config.autoReward) {
        // Bypass the ad entirely and enter online mode by firing the
        // openOnlineMode UnityEvent directly (the reward is granted server-side
        // once online mode starts).
        AutoReward_SwitchToOnline();
        LOGD("OnOnlineModeButtonPressed: intercepted by autoReward");
        return; // Skip original — no ad shown
    }
    orig_OnOnlineModeButtonPressed(instance);
}

// ── CAS SDK ShowAd hook — intercepts rewarded ads at the SDK level ──
typedef void (*ShowAd_t)(void*, int);
static ShowAd_t orig_ShowAd = nullptr;

static void my_ShowAd(void* instance, int adType) {
    // Auto-reward takes priority for rewarded ads: give reward, skip ad
    if (g_config.autoReward && adType == 2) {
        AutoReward_GiveReward();
        LOGD("ShowAd(Rewarded): intercepted by autoReward");
        return; // Skip original — no ad shown
    }
    // Block ALL ads when blockAds is enabled
    if (g_config.blockAds) {
        LOGD("ShowAd(%d): blocked by blockAds", adType);
        return; // Skip original — no ad shown
    }
    orig_ShowAd(instance, adType);
}

// ── IL2CPP lazy init (shared by both render hooks) ──
static std::once_flag g_lazyInitFlag;

static void LazyInitIL2CPP() {
    std::call_once(g_lazyInitFlag, []() {
        LOGI("LazyInit: starting IL2CPP init...");
        if (!IL2CPP::Globals.m_GameAssembly) return;

        if (!IL2CPP::Initialize(IL2CPP::Globals.m_GameAssembly)) {
            LOGE("LazyInit: IL2CPP::Initialize failed");
            return;
        }
        LOGI("LazyInit: IL2CPP ready");

        ResolveOffsets();
        SetupEconomyHooks();

        void* updatePtr = nullptr;
        int t = 0;
        while (!updatePtr && t < 30) {
            updatePtr = IL2CPP::Class::Utils::GetMethodPointer("FP_Controller", "Update", 0);
            if (!updatePtr) { sleep(1); t++; }
        }
        if (updatePtr) {
            DobbyHook(updatePtr, (dobby_dummy_func_t)my_FP_Update,
                      (dobby_dummy_func_t*)&orig_FP_Update);
            LOGI("LazyInit: FP_Controller::Update hooked");
        }

        ESPInit();

        void* btnPtr = IL2CPP::Class::Utils::GetMethodPointer("RewardedAdsForOnline", "OnOnlineModeButtonPressed", 0);
        if (btnPtr) {
            DobbyHook(btnPtr, (dobby_dummy_func_t)my_OnOnlineModeButtonPressed,
                      (dobby_dummy_func_t*)&orig_OnOnlineModeButtonPressed);
            LOGI("LazyInit: RewardedAdsForOnline::OnOnlineModeButtonPressed hooked");
        }

        void* showAdPtr = IL2CPP::Class::Utils::GetMethodPointer("CASManagerClient", "ShowAd", 1);
        if (showAdPtr) {
            DobbyHook(showAdPtr, (dobby_dummy_func_t)my_ShowAd,
                      (dobby_dummy_func_t*)&orig_ShowAd);
            LOGI("LazyInit: CASManagerClient::ShowAd hooked");
        } else {
            LOGE("LazyInit: CASManagerClient::ShowAd NOT found");
        }

        LOGI("LazyInit: done");
    });
}

// Render ESP overlay (throttled ~30fps). Safe to call from any GL draw context.
static void DoESPRender() {
    if (!(g_config.espPlayers || g_config.espObjects)) return;

    static auto g_lastESP = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - g_lastESP < std::chrono::milliseconds(33))
        return;
    g_lastESP = now;

    GLint prevVao, prevVbo;
    GLint prevTexture2D, prevActiveTexture, prevUnpackAlignment, prevSamplerBinding;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2D);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    glGetIntegerv(GL_SAMPLER_BINDING, &prevSamplerBinding);

    CRASH_GUARD(RenderESPGLES());

    glActiveTexture((GLenum)prevActiveTexture);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTexture2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
    glBindSampler(prevActiveTexture - GL_TEXTURE0, (GLuint)prevSamplerBinding);
    glBindVertexArray(prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, prevVbo);
}

// ── glDrawElements hook (GLES mid-frame) ──
typedef void (*glDrawElements_t)(GLenum, GLsizei, GLenum, const void*);
static glDrawElements_t orig_glDrawElements = nullptr;

static void my_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    orig_glDrawElements(mode, count, type, indices);
    if (!g_hooksReady.load(std::memory_order_acquire)) return;
    DoESPRender();
}

// ── eglSwapBuffers hook (fires once per composited frame on every GLES path) ──
typedef void (*eglSwapBuffers_t)(void*, void*);
typedef void (*eglSwapBuffersWithDamageKHR_t)(void*, void*, void*, int);
typedef void* (*eglGetProcAddress_t)(const char*);

static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;
static eglSwapBuffersWithDamageKHR_t orig_eglSwapBuffersWithDamageKHR = nullptr;

static void my_eglSwapBuffers(void* display, void* surface) {
    if (!g_hooksReady.load(std::memory_order_acquire)) {
        if (orig_eglSwapBuffers) orig_eglSwapBuffers(display, surface);
        return;
    }
    LazyInitIL2CPP();
    DoESPRender();
    if (orig_eglSwapBuffers) orig_eglSwapBuffers(display, surface);
}

// Some titles call the KHR damage variant instead of plain eglSwapBuffers.
static void my_eglSwapBuffersWithDamageKHR(void* display, void* surface, void* rects, int n) {
    if (!g_hooksReady.load(std::memory_order_acquire)) {
        if (orig_eglSwapBuffersWithDamageKHR)
            orig_eglSwapBuffersWithDamageKHR(display, surface, rects, n);
        return;
    }
    LazyInitIL2CPP();
    DoESPRender();
    if (orig_eglSwapBuffersWithDamageKHR)
        orig_eglSwapBuffersWithDamageKHR(display, surface, rects, n);
}

void InitNative() {
    LOGI("InitNative called");

    std::thread([]() {
        LOGD("Worker: waiting for libil2cpp...");
        while (!isLibraryLoaded(targetLibName)) sleep(1);

        LOGD("Worker: dlopen libil2cpp...");
        void* handle = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_LAZY);
        if (!handle) {
            LOGE("Worker: failed to dlopen libil2cpp");
            return;
        }
        IL2CPP::Globals.m_GameAssembly = handle;
        LOGI("Worker: libil2cpp handle acquired");

        LOGD("Worker: hook glDrawElements...");
        void* gles = nullptr;
        while (!gles) {
            gles = dlopen("libGLESv2.so", RTLD_LAZY);
            if (!gles) sleep(1);
        }
        void* target = dlsym(gles, "glDrawElements");
        if (target) {
            DobbyHook(target, (dobby_dummy_func_t)my_glDrawElements,
                      (dobby_dummy_func_t*)&orig_glDrawElements);
            LOGI("glDrawElements hooked at %p", target);
        } else {
            LOGE("glDrawElements not found in libGLESv2.so");
        }

        LOGD("Worker: hook eglSwapBuffers (EGL)...");
        void* egl = nullptr;
        while (!egl) {
            egl = dlopen("libEGL.so", RTLD_LAZY);
            if (!egl) sleep(1);
        }

        // Resolve via eglGetProcAddress (version-agnostic: works for EGL 1.4/1.5
        // and through the driver dispatch table), with a dlsym fallback.
        eglGetProcAddress_t eglGetProc = (eglGetProcAddress_t)dlsym(egl, "eglGetProcAddress");
        auto ResolveEGL = [&](const char* name) -> void* {
            void* p = eglGetProc ? eglGetProc(name) : nullptr;
            if (!p) p = dlsym(egl, name);
            return p;
        };

        void* eglTarget = ResolveEGL("eglSwapBuffers");
        if (eglTarget) {
            DobbyHook(eglTarget, (dobby_dummy_func_t)my_eglSwapBuffers,
                      (dobby_dummy_func_t*)&orig_eglSwapBuffers);
            LOGI("eglSwapBuffers hooked at %p", eglTarget);
        } else {
            LOGE("eglSwapBuffers not found");
        }

        void* eglDmg = ResolveEGL("eglSwapBuffersWithDamageKHR");
        if (eglDmg) {
            DobbyHook(eglDmg, (dobby_dummy_func_t)my_eglSwapBuffersWithDamageKHR,
                      (dobby_dummy_func_t*)&orig_eglSwapBuffersWithDamageKHR);
            LOGI("eglSwapBuffersWithDamageKHR hooked at %p", eglDmg);
        } else {
            LOGD("eglSwapBuffersWithDamageKHR not present (ok)");
        }

        g_hooksReady.store(true, std::memory_order_release);
        LOGI("All hooks installed, g_hooksReady=true");
    }).detach();
}

// ── JNI exports (fallback in case RegisterNatives in Setup.cpp fails) ──
extern "C" JNIEXPORT jstring JNICALL
Java_com_android_support_Menu_Icon(JNIEnv *env, jobject thiz) {
    return Icon(env, thiz);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_android_support_Menu_IconWebViewData(JNIEnv *env, jobject thiz) {
    return IconWebViewData(env, thiz);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_android_support_Menu_IsGameLibLoaded(JNIEnv *env, jobject thiz) {
    return isGameLibLoaded(env, thiz);
}

extern "C" JNIEXPORT void JNICALL
Java_com_android_support_Menu_Init(JNIEnv *env, jobject thiz, jobject ctx, jobject title, jobject subtitle) {
    Init(env, thiz, ctx, title, subtitle);
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_android_support_Menu_SettingsList(JNIEnv *env, jobject thiz) {
    return SettingsList(env, thiz);
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_android_support_Menu_GetFeatureList(JNIEnv *env, jobject thiz) {
    return GetFeatureList(env, thiz);
}

extern "C" JNIEXPORT void JNICALL
Java_com_android_support_Preferences_Changes(JNIEnv *env, jclass clazz, jobject obj,
    jint featNum, jstring featName, jint value, jlong Lvalue, jboolean boolean, jstring text) {
    Changes(env, clazz, obj, featNum, featName, value, Lvalue, boolean, text);
}

extern "C" JNIEXPORT void JNICALL
Java_com_android_support_Main_CheckOverlayPermission(JNIEnv *env, jclass clazz, jobject ctx) {
    CheckOverlayPermission(env, clazz, ctx);
}
