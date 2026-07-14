#include "esp.h"
#include "esp_data.h"
#include "il2cpp.h"
#include "config.h"
#include "imgui.h"
#include <android/log.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <unistd.h>

// ══════════════════════════════════════════════════════════════════════
// CRASH GUARD — catches SIGSEGV/SIGBUS so a bad memory read doesn't kill the game
// ── Uses sigaction + siglongjmp to recover safely.
// ══════════════════════════════════════════════════════════════════════

thread_local sigjmp_buf g_crashJmpBuf;
thread_local volatile bool g_crashGuardActive = false;

static void CrashHandler(int sig) {
    if (g_crashGuardActive) {
        g_crashGuardActive = false;
        siglongjmp(g_crashJmpBuf, 1);
    }
    // No guard active — this is a real unexpected crash, terminate
    _exit(1);
}

static thread_local bool g_crashGuardInstalled = false;

void InstallCrashGuard() {
    if (g_crashGuardInstalled) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = CrashHandler;
    sa.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    g_crashGuardInstalled = true;
}

// ── Global shared data (mutex initialized in main.cpp constructor) ──
ESPSharedData g_espData;

// ── ESP configurable visuals ──
static int g_espHealth   = -1;
static int g_espCarrying = -1;
static int g_espValue    = -1;
static int g_espWeight   = -1;
static int g_espHeavy    = -1;
static int g_espItemName = -1;
static bool g_espInit    = false;

// ── Main-thread state ──
static Unity::CCamera* g_cachedCam = nullptr;
static bool g_wasTouching = false;
static int  g_lastScreenW = 0, g_lastScreenH = 0;

static void EnsureOffsets() {
    if (g_espInit) return;
    g_espHealth   = GetFieldOffset("Health", "healthPoints");
    g_espCarrying = GetFieldOffset("InventoryItem", "carrying");
    g_espValue    = GetFieldOffset("InventoryItem", "Value");
    g_espWeight   = GetFieldOffset("InventoryItem", "Weight");
    g_espHeavy    = GetFieldOffset("InventoryItem", "isHeavy");
    g_espItemName = GetFieldOffset("InventoryItem", "itemName");
    g_espInit = true;
    LOGD("ESP offsets: health=%d carrying=%d value=%d weight=%d heavy=%d name=%d",
         g_espHealth, g_espCarrying, g_espValue, g_espWeight, g_espHeavy, g_espItemName);
}

static inline bool IsFinite(float v) { return std::isfinite(v); }

// ── IL2CPP helper for value-type unboxing ──
struct Il2CppObject { void* klass; void* monitor; };
template<typename T>
static T Unbox(void* boxed) {
    if (!boxed) return T{};
    return *(T*)((uint8_t*)boxed + sizeof(Il2CppObject));
}

// ══════════════════════════════════════════════════════════════════════
// MAIN-THREAD FUNCTIONS (called from FP_Controller::Update hook)
// ══════════════════════════════════════════════════════════════════════

void GatherTouchMainThread() {
    // ── Get game screen resolution (cache) ──
    {
        auto* klass = IL2CPP::Class::Find("UnityEngine.Screen");
        if (klass) {
            typedef void* (*GetMethod)(void*, const char*, int);
            auto* getWidth  = ((GetMethod)IL2CPP::Functions.m_ClassGetMethodFromName)(klass, "get_width", 0);
            auto* getHeight = ((GetMethod)IL2CPP::Functions.m_ClassGetMethodFromName)(klass, "get_height", 0);
            if (getWidth && getHeight) {
                static void* s_invoke = nullptr;
                if (!s_invoke) { void* h = dlopen("libil2cpp.so", RTLD_NOLOAD); if (h) s_invoke = dlsym(h, "il2cpp_runtime_invoke"); }
                if (s_invoke) {
                    auto invoke = (void* (*)(void*, void*, void**, void**))s_invoke;
                    void* exc = nullptr;
                    void* rw = invoke(getWidth, nullptr, nullptr, &exc);
                    void* rh = invoke(getHeight, nullptr, nullptr, &exc);
                    if (rw && rh) {
                        g_lastScreenW = Unbox<int>(rw);
                        g_lastScreenH = Unbox<int>(rh);
                    }
                }
            }
        }
    }

    // ── Get mouse position (primary) ──
    struct Vec3 { float x, y, z; };
    float touchX = 0, touchY = 0;
    {
        auto* klass = IL2CPP::Class::Find("UnityEngine.Input");
        if (klass) {
            typedef void* (*GetMethod)(void*, const char*, int);
            auto* getMousePos = ((GetMethod)IL2CPP::Functions.m_ClassGetMethodFromName)(klass, "get_mousePosition", 0);
            if (getMousePos) {
                static void* s_invoke = nullptr;
                if (!s_invoke) { void* h = dlopen("libil2cpp.so", RTLD_NOLOAD); if (h) s_invoke = dlsym(h, "il2cpp_runtime_invoke"); }
                if (s_invoke) {
                    auto invoke = (void* (*)(void*, void*, void**, void**))s_invoke;
                    void* exc = nullptr;
                    void* ret = invoke(getMousePos, nullptr, nullptr, &exc);
                    if (ret) { Vec3 v = Unbox<Vec3>(ret); touchX = v.x; touchY = v.y; }
                }
            }
        }
    }

    // ── Get touch position via Input.touches (fallback when mousePosition returns (0,0) on some Android/Unity versions) ──
    // il2cppArray header on arm64: klass(8) + monitor(8) + bounds(8) + maxLength(8) = 32 bytes.
    // UnityEngine.Touch fields layout: m_FingerId(4) + m_Position.x(4) + m_Position.y(4) + ...
    // So first touch's position is at array offset 32+4 = 36 (x) and 40 (y).
    if (touchX == 0.0f && touchY == 0.0f) {
        auto* klass = IL2CPP::Class::Find("UnityEngine.Input");
        if (klass) {
            typedef void* (*GetMethod)(void*, const char*, int);
            auto* getTouches = ((GetMethod)IL2CPP::Functions.m_ClassGetMethodFromName)(klass, "get_touches", 0);
            if (getTouches) {
                static void* s_invoke = nullptr;
                if (!s_invoke) { void* h = dlopen("libil2cpp.so", RTLD_NOLOAD); if (h) s_invoke = dlsym(h, "il2cpp_runtime_invoke"); }
                if (s_invoke) {
                    auto invoke = (void* (*)(void*, void*, void**, void**))s_invoke;
                    void* exc = nullptr;
                    void* ret = invoke(getTouches, nullptr, nullptr, &exc);
                    if (ret) {
                        // Read maxLength from array header at offset 16+8=24 (bounds is after il2cppObject)
                        uintptr_t maxLen = *(uintptr_t*)((uint8_t*)ret + 24);
                        if (maxLen > 0) {
                            // First element starts at byte 32; m_Position is at offset 4
                            uint8_t* elem = (uint8_t*)ret + 32;
                            float tx = *(float*)(elem + 4);
                            float ty = *(float*)(elem + 8);
                            // Only override if we got a non-zero position (genuine touch, not a stray)
                            if (tx != 0.0f || ty != 0.0f) {
                                touchX = tx;
                                touchY = ty;
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Get touch count ──
    int touchCount = 0;
    {
        auto* klass = IL2CPP::Class::Find("UnityEngine.Input");
        if (klass) {
            typedef void* (*GetMethod)(void*, const char*, int);
            auto* getTC = ((GetMethod)IL2CPP::Functions.m_ClassGetMethodFromName)(klass, "get_touchCount", 0);
            if (getTC) {
                static void* s_invoke = nullptr;
                if (!s_invoke) { void* h = dlopen("libil2cpp.so", RTLD_NOLOAD); if (h) s_invoke = dlsym(h, "il2cpp_runtime_invoke"); }
                if (s_invoke) {
                    auto invoke = (void* (*)(void*, void*, void**, void**))s_invoke;
                    void* exc = nullptr;
                    void* ret = invoke(getTC, nullptr, nullptr, &exc);
                    if (ret) touchCount = Unbox<int>(ret);
                }
            }
        }
    }

    bool isTouching = (touchCount > 0);
    bool touchBegan = isTouching && !g_wasTouching;
    bool touchEnded = !isTouching && g_wasTouching;
    g_wasTouching = isTouching;

    pthread_mutex_lock(&g_espData.mutex);
    g_espData.touch.x         = touchX;
    g_espData.touch.y         = touchY;
    g_espData.touch.isDown    = touchBegan;
    g_espData.touch.isUp      = touchEnded;
    g_espData.touch.isTouching = isTouching;
    g_espData.touch.fresh     = true;
    g_espData.gameW           = (float)g_lastScreenW;
    g_espData.gameH           = (float)g_lastScreenH;
    pthread_mutex_unlock(&g_espData.mutex);
}

// ── Scene-change detection ──
// Tracks camera null→valid transitions to detect scene loads.
static bool g_prevCameraHadTransform = false;

void GatherESPMainThread() {
    EnsureOffsets();
    if (!g_config.espPlayers && !g_config.espObjects) {
        // ESP disabled — clear stale data from previous scene
        pthread_mutex_lock(&g_espData.mutex);
        g_espData.dataReady = false;
        g_espData.playerCount = 0;
        g_espData.itemCount = 0;
        pthread_mutex_unlock(&g_espData.mutex);
        g_prevCameraHadTransform = false;
        return;
    }

    // ── Get fresh camera (no caching — scene changes destroy the old one) ──
    g_cachedCam = Unity::Camera::GetMain();
    if (!g_cachedCam) {
        // No camera in this scene — clear data and stop rendering
        pthread_mutex_lock(&g_espData.mutex);
        g_espData.dataReady = false;
        g_espData.playerCount = 0;
        g_espData.itemCount = 0;
        pthread_mutex_unlock(&g_espData.mutex);
        g_prevCameraHadTransform = false;
        return;
    }
    // Verify camera is alive by checking its Transform
    {
        Unity::CTransform* testTx = reinterpret_cast<Unity::CComponent*>(g_cachedCam)->GetTransform();
        if (!testTx) {
            pthread_mutex_lock(&g_espData.mutex);
            g_espData.dataReady = false;
            g_espData.playerCount = 0;
            g_espData.itemCount = 0;
            pthread_mutex_unlock(&g_espData.mutex);
            g_prevCameraHadTransform = false;
            return;
        }
    }

    float maxDist = IsFinite(g_config.espMaxDist) ? g_config.espMaxDist : 30.0f;

    // ── Lock and populate shared buffer ──
    pthread_mutex_lock(&g_espData.mutex);

    g_espData.drawPlayers   = g_config.espPlayers;
    g_espData.drawObjects   = g_config.espObjects;
    g_espData.drawBoxes     = g_config.espBoxes;
    g_espData.drawTracelines = g_config.espTracelines;
    g_espData.drawLabels    = g_config.espLabels;
    g_espData.maxDist       = maxDist;
    g_espData.playerCount   = 0;
    g_espData.itemCount     = 0;

    // ── Gather players ──
    if (g_config.espPlayers) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("PlayerHealth");
        if (arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength && g_espData.playerCount < MAX_ESP_PLAYERS; i++) {
                void* ph = arr->At(i);
                if (!ph) continue;

                Unity::CComponent* comp = reinterpret_cast<Unity::CComponent*>(ph);
                Unity::CTransform* tx = comp ? comp->GetTransform() : nullptr;
                if (!tx) continue;
                Unity::Vector3 wpos = tx->GetPosition();
                if (!IsFinite(wpos.X) || !IsFinite(wpos.Y) || !IsFinite(wpos.Z)) continue;

                Unity::Vector3 spos;
                g_cachedCam->WorldToScreen(wpos, spos, 2);
                if (!IsFinite(spos.X) || !IsFinite(spos.Y) || !IsFinite(spos.Z)) continue;
                if (spos.Z < 0.5f || spos.Z > maxDist) continue;

                float hp = (g_espHealth >= 0) ? *(float*)((uintptr_t)ph + g_espHealth) : 100.0f;
                if (!IsFinite(hp)) hp = 0; if (hp < 0) hp = 0;

                int idx = g_espData.playerCount++;
                g_espData.players[idx].sx   = spos.X;  // game-space (scaled in render)
                g_espData.players[idx].sy   = spos.Y;
                g_espData.players[idx].dist = spos.Z;
                g_espData.players[idx].hp   = hp;
            }
        }
    }

    // ── Gather items ──
    if (g_config.espObjects) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("InventoryItem");
        if (arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength && g_espData.itemCount < MAX_ESP_ITEMS; i++) {
                void* item = arr->At(i);
                if (!item) continue;
                if (g_espCarrying >= 0 && *(bool*)((uintptr_t)item + g_espCarrying)) continue;

                Unity::CComponent* comp = reinterpret_cast<Unity::CComponent*>(item);
                Unity::CTransform* tx = comp ? comp->GetTransform() : nullptr;
                if (!tx) continue;
                Unity::Vector3 wpos = tx->GetPosition();
                if (!IsFinite(wpos.X) || !IsFinite(wpos.Y) || !IsFinite(wpos.Z)) continue;

                Unity::Vector3 spos;
                g_cachedCam->WorldToScreen(wpos, spos, 2);
                if (!IsFinite(spos.X) || !IsFinite(spos.Y) || !IsFinite(spos.Z)) continue;
                if (spos.Z < 0.5f || spos.Z > maxDist) continue;

                int idx = g_espData.itemCount++;
                g_espData.items[idx].sx      = spos.X;
                g_espData.items[idx].sy      = spos.Y;
                g_espData.items[idx].dist    = spos.Z;
                g_espData.items[idx].value   = (g_espValue >= 0) ? *(int*)((uintptr_t)item + g_espValue) : 0;
                g_espData.items[idx].weight  = (g_espWeight >= 0) ? *(float*)((uintptr_t)item + g_espWeight) : 0.0f;
                g_espData.items[idx].isHeavy = (g_espHeavy >= 0) ? *(bool*)((uintptr_t)item + g_espHeavy) : false;
            }
        }
    }

    // If both counts are zero, scene might have unloaded — don't render stale data
    g_espData.dataReady = (g_espData.playerCount > 0 || g_espData.itemCount > 0);
    pthread_mutex_unlock(&g_espData.mutex);
    g_prevCameraHadTransform = true;
}

// ══════════════════════════════════════════════════════════════════════
// ESP BACKGROUND WORKER THREAD
// ── Runs GatherESPMainThread() on a dedicated thread so FindObjectsOfType
//    and WorldToScreen never block the main thread's touch input pipeline.
// ══════════════════════════════════════════════════════════════════════

#include <pthread.h>

static pthread_t g_espWorker        = 0;
static volatile bool g_espWorkerRunning = false;
static volatile bool g_espNeedsRefresh  = false;

void TriggerESPRefresh() {
    g_espNeedsRefresh = true;
}

// Try to attach this thread to the IL2CPP domain so managed calls work.
static void AttachToIL2CPPDomain() {
    if (IL2CPP::Functions.m_ThreadAttach && IL2CPP::Functions.m_DomainGet) {
        typedef void* (*ThreadAttach_t)(void*);
        typedef void* (*DomainGet_t)();
        void* domain = ((DomainGet_t)IL2CPP::Functions.m_DomainGet)();
        if (domain)
            ((ThreadAttach_t)IL2CPP::Functions.m_ThreadAttach)(domain);
    }
}

static void* ESPWorkerLoop(void*) {
    LOGD("ESP worker thread started");
    AttachToIL2CPPDomain();

    // ── Refresh frame counter: throttle ESP to ~1 refresh per 10 worker cycles ──
    int cycle = 0;

    while (g_espWorkerRunning) {
        ++cycle;
        bool shouldRefresh = g_espNeedsRefresh;
        bool sceneJustLoaded = false;

        // ── Scene-change detection (only when ESP is active) ──
        // Detect when camera becomes valid after being null (scene loaded).
        // Bypasses throttle so ESP appears immediately in the new scene.
        if ((g_config.espPlayers || g_config.espObjects) && !g_prevCameraHadTransform) {
            Unity::CCamera* testCam = Unity::Camera::GetMain();
            if (testCam) {
                Unity::CTransform* testTx = reinterpret_cast<Unity::CComponent*>(testCam)->GetTransform();
                if (testTx) {
                    sceneJustLoaded = true;
                    LOGD("ESP: scene change detected — forced immediate refresh");
                }
            }
        }

        if (shouldRefresh) {
            g_espNeedsRefresh = false;
            cycle = 0;
        }

        // Run ESP: on scene change (immediate), on trigger, or every 3rd cycle
        bool shouldRun = shouldRefresh || sceneJustLoaded || cycle >= 3;
        if (shouldRun && (g_config.espPlayers || g_config.espObjects)) {
            if (!shouldRefresh && !sceneJustLoaded) cycle = 0;
            if (sceneJustLoaded) cycle = 0;
            CRASH_GUARD(GatherESPMainThread());
        }

        // 50ms sleep ≈ 20fps ESP refresh — plenty for visual overlay, and keeps
        // CPU usage low on the background thread.
        usleep(50000);
    }

    LOGD("ESP worker thread stopped");
    return nullptr;
}

void StartESPWorkerThread() {
    if (g_espWorkerRunning) return;
    g_espWorkerRunning = true;
    pthread_create(&g_espWorker, nullptr, ESPWorkerLoop, nullptr);
    LOGD("ESP worker thread created");
}

void StopESPWorkerThread() {
    g_espWorkerRunning = false;
    if (g_espWorker) {
        pthread_join(g_espWorker, nullptr);
        g_espWorker = 0;
        LOGD("ESP worker thread joined");
    }
}

// ══════════════════════════════════════════════════════════════════════
// RENDER-THREAD FUNCTIONS (called from eglSwapBuffers hook)
// ══════════════════════════════════════════════════════════════════════

// ── Drawing helpers (same as before) ──
static void DrawTextShadow(ImDrawList* dl, const ImVec2& pos, uint32_t col, const char* text) {
    uint32_t shadow = IM_COL32(0, 0, 0, 200);
    dl->AddText(ImVec2(pos.x + 1, pos.y + 1), shadow, text);
    dl->AddText(pos, col, text);
}

static void DrawCornerBox(ImDrawList* dl, float x, float y, float w, float h, uint32_t col, float thickness) {
    float c = 8.0f;
    dl->AddLine(ImVec2(x, y + c), ImVec2(x, y), col, thickness);
    dl->AddLine(ImVec2(x, y), ImVec2(x + c, y), col, thickness);
    dl->AddLine(ImVec2(x + w - c, y), ImVec2(x + w, y), col, thickness);
    dl->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + c), col, thickness);
    dl->AddLine(ImVec2(x, y + h - c), ImVec2(x, y + h), col, thickness);
    dl->AddLine(ImVec2(x, y + h), ImVec2(x + c, y + h), col, thickness);
    dl->AddLine(ImVec2(x + w - c, y + h), ImVec2(x + w, y + h), col, thickness);
    dl->AddLine(ImVec2(x + w, y + h - c), ImVec2(x + w, y + h), col, thickness);
}

static void DrawDiamond(ImDrawList* dl, float cx, float cy, float radius, uint32_t col, float thickness) {
    dl->AddLine(ImVec2(cx, cy - radius), ImVec2(cx + radius, cy), col, thickness);
    dl->AddLine(ImVec2(cx + radius, cy), ImVec2(cx, cy + radius), col, thickness);
    dl->AddLine(ImVec2(cx, cy + radius), ImVec2(cx - radius, cy), col, thickness);
    dl->AddLine(ImVec2(cx - radius, cy), ImVec2(cx, cy - radius), col, thickness);
}

void RenderESPRenderThread() {
    if (!g_espData.dataReady) return;

    float sw = ImGui::GetIO().DisplaySize.x;
    float sh = ImGui::GetIO().DisplaySize.y;
    if (sw < 1.0f || sh < 1.0f) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    // ── Read shared data under lock ──
    pthread_mutex_lock(&g_espData.mutex);

    bool drawPlayers   = g_espData.drawPlayers;
    bool drawObjects   = g_espData.drawObjects;
    bool drawBoxes     = g_espData.drawBoxes;
    bool drawTracelines = g_espData.drawTracelines;
    bool drawLabels    = g_espData.drawLabels;
    float maxDist      = g_espData.maxDist;
    float gameW        = g_espData.gameW;
    float gameH        = g_espData.gameH;

    // Scale factors: game-space → physical surface
    float scaleX = (gameW > 0) ? sw / gameW : 1.0f;
    float scaleY = (gameH > 0) ? sh / gameH : 1.0f;

    // ── Draw players ──
    if (drawPlayers) {
        for (int i = 0; i < g_espData.playerCount; i++) {
            ESPPlayer& p = g_espData.players[i];
            float sx = p.sx * scaleX;
            float sy = sh - (p.sy * scaleY);
            float dist = p.dist;
            float hp = p.hp;
            if (sx < -5000.0f || sx > sw + 5000.0f || sy < -5000.0f || sy > sh + 5000.0f) continue;

            float boxW = 55.0f * (1.0f - fminf(dist / maxDist, 0.5f));
            float boxH = boxW * 2.0f;

            uint32_t boxCol, glowCol;
            float hpRatio = (hp > 100.0f) ? 1.0f : hp / 100.0f;
            if (hpRatio > 0.5f) {
                boxCol = IM_COL32(0, 255, 80, 230); glowCol = IM_COL32(0, 255, 80, 40);
            } else if (hpRatio > 0.25f) {
                boxCol = IM_COL32(255, 255, 0, 230); glowCol = IM_COL32(255, 255, 0, 40);
            } else {
                boxCol = IM_COL32(255, 40, 40, 230); glowCol = IM_COL32(255, 40, 40, 40);
            }

            float left = sx - boxW / 2.0f;
            float top  = sy - boxH;

            if (drawTracelines)
                dl->AddLine(ImVec2(sw * 0.5f, sh), ImVec2(sx, sy), IM_COL32(255, 255, 255, 30), 1.0f);

            if (drawBoxes) {
                dl->AddRectFilled(ImVec2(left - 2, top - 2), ImVec2(left + boxW + 2, sy + 2), glowCol);
                DrawCornerBox(dl, left, top, boxW, boxH, boxCol, 2.0f);
                dl->AddCircleFilled(ImVec2(sx, top), 3.0f, boxCol);

                float barW = 4.0f;
                float barLeft = left - barW - 3.0f;
                dl->AddRectFilled(ImVec2(barLeft, top), ImVec2(barLeft + barW, sy), IM_COL32(0, 0, 0, 200));
                float barH = boxH * hpRatio;
                if (barH > 0) dl->AddRectFilled(ImVec2(barLeft, sy - barH), ImVec2(barLeft + barW, sy), boxCol);
                dl->AddRect(ImVec2(barLeft, top), ImVec2(barLeft + barW, sy), IM_COL32(0, 0, 0, 100), 0, 0, 1.0f);
            }

            if (drawLabels) {
                char buf[80];
                snprintf(buf, sizeof(buf), "%.0fm", dist);
                float textW = ImGui::CalcTextSize(buf).x;
                DrawTextShadow(dl, ImVec2(sx - textW * 0.5f, sy + 3), boxCol, buf);
                snprintf(buf, sizeof(buf), "HP %.0f", hp);
                textW = ImGui::CalcTextSize(buf).x;
                DrawTextShadow(dl, ImVec2(sx - textW * 0.5f, drawBoxes ? top - 14 : sy - 14), boxCol, buf);
            }
        }
    }

    // ── Draw items ──
    if (drawObjects) {
        for (int i = 0; i < g_espData.itemCount; i++) {
            ESPItem& it = g_espData.items[i];
            float sx = it.sx * scaleX;
            float sy = sh - (it.sy * scaleY);
            float dist = it.dist;
            if (sx < -5000.0f || sx > sw + 5000.0f || sy < -5000.0f || sy > sh + 5000.0f) continue;

            int val = it.value;
            float weight = it.weight;
            bool isHeavy = it.isHeavy;

            uint32_t col, fillCol, glowCol;
            float radius = 10.0f;
            if (val >= 80) {
                col = IM_COL32(255, 215, 0, 255); fillCol = IM_COL32(255, 215, 0, 50); glowCol = IM_COL32(255, 215, 0, 20); radius = 13.0f;
            } else if (val >= 40) {
                col = IM_COL32(0, 230, 80, 240); fillCol = IM_COL32(0, 230, 80, 35); glowCol = IM_COL32(0, 230, 80, 15);
            } else if (val >= 20) {
                col = IM_COL32(180, 230, 255, 220); fillCol = IM_COL32(180, 230, 255, 25); glowCol = 0;
            } else if (val > 0) {
                col = IM_COL32(160, 160, 160, 200); fillCol = IM_COL32(160, 160, 160, 20); glowCol = 0;
            } else {
                col = IM_COL32(80, 80, 80, 150); fillCol = 0; glowCol = 0;
            }

            if (glowCol) dl->AddCircleFilled(ImVec2(sx, sy), radius + 4, glowCol);
            if (fillCol) dl->AddCircleFilled(ImVec2(sx, sy), radius + 2, fillCol);

            float thickness = (val >= 80) ? 3.0f : 2.0f;
            DrawDiamond(dl, sx, sy, radius, col, thickness);

            if (val >= 80)
                dl->AddText(ImVec2(sx - 10, sy - radius - 16), IM_COL32(255, 50, 50, 255), "!!");

            if (drawLabels) {
                char buf[140]; int off = 0;
                if (val > 0) off += snprintf(buf + off, sizeof(buf) - off, "$%d", val);
                else off += snprintf(buf + off, sizeof(buf) - off, "$?");
                if (weight > 0.0f) off += snprintf(buf + off, sizeof(buf) - off, " %.1fkg", weight);
                if (isHeavy) off += snprintf(buf + off, sizeof(buf) - off, " [H]");
                off += snprintf(buf + off, sizeof(buf) - off, "  %.0fm", dist);
                DrawTextShadow(dl, ImVec2(sx + radius + 8, sy - 9), col, buf);
            }
        }
    }

    pthread_mutex_unlock(&g_espData.mutex);
}
