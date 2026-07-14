#include <jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <string>
#include "hook.h"
#include "il2cpp.h"
#include "config.h"
#include "cheats.h"
#include "esp.h"
#include "esp_data.h"
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_android.h"

#ifndef LOGD
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "FBK", __VA_ARGS__)
#endif
#ifndef LOGE
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FBK", __VA_ARGS__)
#endif

// ── State ──
static bool g_imguiReady   = false;
static bool g_il2cppReady  = false;
static bool g_il2cppInitAttempted = false;
static bool g_hookOK       = false;
static bool g_inputOK      = false;
static bool g_mainHookOK   = false;
static double g_LastTime   = 0.0;
static bool g_collapsed    = true;
static pthread_t g_worker;

static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

// ══════════════════════════════════════════════════════════════════════
// NATIVE TOUCH — hooks AInputQueue_finishEvent to feed events directly
// to ImGui via the official Android backend. Works in ANY scene because
// Android's input system is always active.
// ══════════════════════════════════════════════════════════════════════

typedef void (*AInputQueue_finishEvent_t)(void* queue, void* event, int handled);
static AInputQueue_finishEvent_t orig_AInputQueue_finishEvent = nullptr;

static void my_AInputQueue_finishEvent(void* queue, void* event, int handled) {
    if (!event) {
        if (orig_AInputQueue_finishEvent)
            orig_AInputQueue_finishEvent(queue, nullptr, handled);
        return;
    }

    // Feed event to ImGui via the official Android backend
    if (g_imguiReady)
        ImGui_ImplAndroid_HandleInputEvent((const AInputEvent*)event);

    // Mark input as OK as soon as we see any event
    if (!g_inputOK) g_inputOK = true;

    // ── Block game input when the UI is expanded and ImGui is consuming touch ──
    // This prevents the game from receiving taps/swipes while the player is using
    // the cheat menu. When collapsed, WantCaptureMouse handles the FBK button.
    if (g_imguiReady && !g_collapsed) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            // ImGui is using this touch — eat it, don't forward to the game
            if (orig_AInputQueue_finishEvent)
                orig_AInputQueue_finishEvent(queue, event, 1);
            return;
        }
    }

    if (orig_AInputQueue_finishEvent)
        orig_AInputQueue_finishEvent(queue, event, handled);
}

extern "C" bool ImGui_ImplOpenGL3_LoadFunctions() { return true; }

// ══════════════════════════════════════════════════════════════════════
// MAIN-THREAD HOOK (FP_Controller::Update)
// ══════════════════════════════════════════════════════════════════════

typedef void (*FP_Update_t)(void*);
static FP_Update_t orig_FP_Update = nullptr;
static int g_mainUpdateCount = 0;

static void my_FP_Update(void* instance) {
    // Call original Update first (don't break game logic)
    if (orig_FP_Update) orig_FP_Update(instance);

    ++g_mainUpdateCount;

    // ── Apply cheats every 5 Update calls (slot-throttled) ──
    // Note: GatherTouchMainThread() has been moved to the render thread (my_eglSwapBuffers)
    // so it works in ALL scenes, not just ones with FP_Controller.
    // Touch data is fed through g_espData.touch which is mutex-protected.
    static int g_cheatFrame = 4;
    if (++g_cheatFrame >= 5) {
        g_cheatFrame = 0;
        CRASH_GUARD(ApplyCheats());
        CRASH_GUARD(CheckConfigReload());
    }

    // ── Trigger ESP refresh on background worker thread ──
    // The actual FindObjectsOfType / WorldToScreen work runs on a dedicated
    // background thread so the main thread stays responsive for touch input.
    if (g_config.espPlayers || g_config.espObjects)
        CRASH_GUARD(TriggerESPRefresh());
}

// ── Try to hook FP_Controller::Update (retry until class is resolvable) ──
static void TryHookMainThread() {
    if (g_mainHookOK) return;

    void* ptr = IL2CPP::Class::Utils::GetMethodPointer("FP_Controller", "Update", 0);
    if (ptr) {
        hook_func(ptr, (void*)my_FP_Update, (void**)&orig_FP_Update);
        g_mainHookOK = true;
        LOGD("FP_Controller::Update hooked - cheats running on main thread");
        // Start ESP worker thread now that IL2CPP is fully initialized
        StartESPWorkerThread();
    }
}

// ══════════════════════════════════════════════════════════════════════
// RENDER-THREAD (eglSwapBuffers hook) — rendering & UI only
// ══════════════════════════════════════════════════════════════════════

// ── Helper: check and log GL errors ──
static void CheckGLError(const char* tag) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGE("GL ERROR at %s: 0x%x", tag, err);
    }
}

// ── Helper: save/restore critical GL state ──
struct GLStateBackup {
    GLint lastViewport[4], lastScissorBox[4];
    GLboolean lastBlend, lastDepthTest, lastCullFace, lastScissorTest;
    GLint lastBlendSrcRGB, lastBlendDstRGB, lastBlendSrcAlpha, lastBlendDstAlpha;
    GLint lastBlendEquationRGB, lastBlendEquationAlpha;
    GLint lastProgram, lastVAO, lastArrayBuffer, lastElementBuffer;
    GLboolean lastEnablePrimitiveRestart;
};

static void BackupGLState(GLStateBackup& s) {
    glGetIntegerv(GL_VIEWPORT, s.lastViewport);
    glGetIntegerv(GL_SCISSOR_BOX, s.lastScissorBox);
    s.lastBlend = glIsEnabled(GL_BLEND);
    s.lastDepthTest = glIsEnabled(GL_DEPTH_TEST);
    s.lastCullFace = glIsEnabled(GL_CULL_FACE);
    s.lastScissorTest = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_BLEND_SRC_RGB, &s.lastBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &s.lastBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.lastBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &s.lastBlendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &s.lastBlendEquationRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.lastBlendEquationAlpha);
    glGetIntegerv(GL_CURRENT_PROGRAM, &s.lastProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.lastVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.lastArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &s.lastElementBuffer);
    s.lastEnablePrimitiveRestart = glIsEnabled(GL_PRIMITIVE_RESTART_FIXED_INDEX);
}

static void RestoreGLState(const GLStateBackup& s) {
    glUseProgram(s.lastProgram);
    glBindVertexArray(s.lastVAO);
    glBindBuffer(GL_ARRAY_BUFFER, s.lastArrayBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.lastElementBuffer);
    glBlendEquationSeparate(s.lastBlendEquationRGB, s.lastBlendEquationAlpha);
    glBlendFuncSeparate(s.lastBlendSrcRGB, s.lastBlendDstRGB, s.lastBlendSrcAlpha, s.lastBlendDstAlpha);
    if (s.lastBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (s.lastDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (s.lastCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (s.lastScissorTest) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (s.lastEnablePrimitiveRestart) glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX); else glDisable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
    glViewport(s.lastViewport[0], s.lastViewport[1], s.lastViewport[2], s.lastViewport[3]);
    glScissor(s.lastScissorBox[0], s.lastScissorBox[1], s.lastScissorBox[2], s.lastScissorBox[3]);
}

static EGLBoolean my_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    // ── IL2CPP init ──
    if (!g_il2cppInitAttempted && IL2CPP::Globals.m_GameAssembly) {
        g_il2cppInitAttempted = true;
        if (IL2CPP::UnityAPI::Initialize()) {
            g_il2cppReady = true;
            LoadConfig();
            LOGD("il2cpp ready");
        }
    }

    // ── Set up main thread hook (retry each frame until FP_Controller is resolvable) ──
    if (g_il2cppReady && !g_mainHookOK) {
        TryHookMainThread();
    }

    // ── ImGui one-time init ──
    if (!g_imguiReady) {
        LOGD("Starting ImGui init...");
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
        // ── Touch-friendly styling ──
        // Expand hitboxes: fingers are ≈10mm wide (~40px at 400dpi), so generous
        // padding ensures one-tap accuracy on any widget.
        ImGui::GetStyle().TouchExtraPadding = ImVec2(16.0f, 16.0f);
        // Increase slider/scrollbar grab size so they're not impossible to drag
        ImGui::GetStyle().GrabMinSize       = 24.0f;
        // Larger frame padding for checkbox/button hit areas
        ImGui::GetStyle().FramePadding      = ImVec2(8.0f, 6.0f);
        // More spacing between items to prevent accidental taps
        ImGui::GetStyle().ItemSpacing       = ImVec2(12.0f, 8.0f);
        // Increase scrollbar width for touch dragging
        ImGui::GetStyle().ScrollbarSize     = 20.0f;

        unsigned char* font_pixels; int font_w, font_h;
        io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_w, &font_h);
        LOGD("Font atlas size: %dx%d", font_w, font_h);

        ImGui::StyleColorsDark();
        auto& s = ImGui::GetStyle();
        s.Colors[ImGuiCol_WindowBg]          = ImVec4(0.10f, 0.05f, 0.05f, 0.94f);
        s.Colors[ImGuiCol_TitleBg]           = ImVec4(0.50f, 0.06f, 0.06f, 1.00f);
        s.Colors[ImGuiCol_TitleBgActive]     = ImVec4(0.70f, 0.08f, 0.08f, 1.00f);
        s.Colors[ImGuiCol_Button]            = ImVec4(0.55f, 0.06f, 0.06f, 0.80f);
        s.Colors[ImGuiCol_ButtonHovered]     = ImVec4(0.75f, 0.08f, 0.08f, 1.00f);
        s.Colors[ImGuiCol_ButtonActive]      = ImVec4(0.90f, 0.10f, 0.10f, 1.00f);
        s.Colors[ImGuiCol_Header]            = ImVec4(0.55f, 0.06f, 0.06f, 0.70f);
        s.Colors[ImGuiCol_HeaderHovered]     = ImVec4(0.70f, 0.08f, 0.08f, 0.80f);
        s.Colors[ImGuiCol_HeaderActive]      = ImVec4(0.85f, 0.10f, 0.10f, 1.00f);
        s.Colors[ImGuiCol_FrameBg]           = ImVec4(0.15f, 0.06f, 0.06f, 0.70f);
        s.Colors[ImGuiCol_FrameBgHovered]    = ImVec4(0.25f, 0.08f, 0.08f, 0.80f);
        s.Colors[ImGuiCol_FrameBgActive]     = ImVec4(0.30f, 0.10f, 0.10f, 1.00f);
        s.Colors[ImGuiCol_CheckMark]         = ImVec4(0.90f, 0.15f, 0.15f, 1.00f);
        s.Colors[ImGuiCol_SliderGrab]        = ImVec4(0.70f, 0.08f, 0.08f, 1.00f);
        s.Colors[ImGuiCol_SliderGrabActive]  = ImVec4(0.90f, 0.10f, 0.10f, 1.00f);
        s.Colors[ImGuiCol_Tab]               = ImVec4(0.40f, 0.05f, 0.05f, 0.80f);
        s.Colors[ImGuiCol_TabHovered]        = ImVec4(0.65f, 0.08f, 0.08f, 0.90f);
        s.Colors[ImGuiCol_TabActive]         = ImVec4(0.70f, 0.08f, 0.08f, 1.00f);
        s.Colors[ImGuiCol_TabUnfocused]      = ImVec4(0.25f, 0.04f, 0.04f, 0.70f);
        s.Colors[ImGuiCol_Text]              = ImVec4(0.95f, 0.85f, 0.85f, 1.00f);

        EGLint physW = 0, physH = 0;
        eglQuerySurface(dpy, surf, EGL_WIDTH, &physW);
        eglQuerySurface(dpy, surf, EGL_HEIGHT, &physH);
        io.DisplaySize = ImVec2((float)physW, (float)physH);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        LOGD("Physical size: %dx%d", physW, physH);

        bool glInitOk = ImGui_ImplOpenGL3_Init("#version 300 es");
        if (!glInitOk) { LOGE("GLES 3.0 init failed, trying GLES 2.0..."); glInitOk = ImGui_ImplOpenGL3_Init("#version 100"); }
        CheckGLError("after ImGui_ImplOpenGL3_Init");

        if (!glInitOk) { LOGE("ImGui OpenGL3 init failed completely"); ImGui::DestroyContext(); return orig_eglSwapBuffers(dpy, surf); }

        float scale = fmaxf(1.0f, (float)physW / 1920.0f);
        io.FontGlobalScale = scale;
        LOGD("UI scale: %.2f (surface %dx%d)", scale, physW, physH);

        g_imguiReady = true;
        LOGD("ImGui init complete");
    }

    ImGuiIO& io = ImGui::GetIO();

    // ── Delta time ──
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    double delta = now - g_LastTime;
    io.DeltaTime = (g_LastTime > 0.0 && delta > 0.0) ? (float)delta : (1.0f / 60.0f);
    g_LastTime = now;

    // ── Get EGL surface size (physical pixels) ──
    EGLint physW = 0, physH = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &physW);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &physH);
    if (physW <= 0 || physH <= 0) return orig_eglSwapBuffers(dpy, surf);
    io.DisplaySize = ImVec2((float)physW, (float)physH);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // ── Feed touch from the native Android backend — handles all event types ──
    // Touch events are fed directly by ImGui_ImplAndroid_HandleInputEvent from
    // the AInputQueue_finishEvent hook (runs on Unity's input thread, ALL scenes).
    // NO Unity Input API calls here — those would deadlock on the render thread.
    {
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    }

    // ── Gesture processing: touch-drag-to-scroll + long-press → right-click ──
    // These run before NewFrame() so ImGui processes them in the current frame.
    {
        // ── Static state for gesture tracking ──
        static float g_scrollPrevY       = 0.0f;
        static bool  g_scrollTracking    = false;
        static bool  g_longPressFired    = false;
        static bool  g_wasDown           = false;
        static float g_longPressStartX   = 0.0f;
        static float g_longPressStartY   = 0.0f;

        // ── Touch-drag-to-scroll ──
        // When dragging on empty space (no widget active), convert vertical finger
        // movement into scroll wheel events so scrollable panels respond naturally.
        if (io.MouseDown[0]) {
            if (!g_scrollTracking) {
                g_scrollPrevY = io.MousePos.y;
                g_scrollTracking = true;
            }
            // Only synthesize scroll when no widget is being actively manipulated
            // (e.g., not dragging a slider, not holding a button)
            if (!ImGui::IsAnyItemActive()) {
                float dy = io.MousePos.y - g_scrollPrevY;
                if (fabsf(dy) > 1.0f) {
                    // Negative dy = finger moving up → positive scroll = scroll up
                    io.AddMouseWheelEvent(0.0f, dy * -0.03f);
                }
            }
            g_scrollPrevY = io.MousePos.y;
        } else {
            g_scrollTracking = false;
        }

        // ── Long-press → right-click ──
        // Hold finger still for >500ms within a ~10px radius to trigger a
        // right-click event. Useful for context menus and secondary actions.
        // Uses manual g_wasDown tracking to avoid depending on MouseDownDuration
        // timing (it's only updated during NewFrame, which hasn't run yet).
        if (io.MouseDown[0]) {
            // Just pressed this frame — record start position
            if (!g_wasDown) {
                g_longPressStartX = io.MousePos.x;
                g_longPressStartY = io.MousePos.y;
                g_longPressFired = false;
            }

            if (!g_longPressFired && io.MouseDownDuration[0] > 0.5f) {
                float dx = io.MousePos.x - g_longPressStartX;
                float dy = io.MousePos.y - g_longPressStartY;
                if (dx * dx + dy * dy < 100.0f) { // within 10px of initial touch
                    // Cancel the active left-click so the user doesn't get both
                    // a checkbox toggle AND a right-click on the same gesture
                    io.AddMouseButtonEvent(0, false);
                    io.AddMouseButtonEvent(1, true);  // right-click down
                    io.AddMouseButtonEvent(1, false); // right-click up
                    g_longPressFired = true;
                }
            }
        } else {
            g_longPressFired = false;
        }
        g_wasDown = io.MouseDown[0];
    }

    // ── New Frame ──
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    // ── ESP (rendered from main-thread-gathered shared buffer, no Unity APIs here) ──
    RenderESPRenderThread();

    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    float uiScale = fmaxf(1.0f, sw / 1920.0f);

    // ── UI ──
    if (g_collapsed) {
        // ── Collapsed launcher — large pill button on right edge ──
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * uiScale, 10.0f * uiScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * uiScale);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.08f, 0.08f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.10f, 0.10f, 0.95f));

        ImGui::SetNextWindowPos(ImVec2(sw - 60.0f * uiScale, sh * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##fbk", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground);
        {
            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            float cx = fmaxf(0.0f, fminf(wp.x, sw - ws.x));
            float cy = fmaxf(0.0f, fminf(wp.y, sh - ws.y));
            if (cx != wp.x || cy != wp.y) ImGui::SetWindowPos(ImVec2(cx, cy));
        }
        if (ImGui::Button("FBK")) g_collapsed = false;
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    } else {
        ImGui::SetNextWindowPos(ImVec2(sw * 0.5f, sh * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::Begin("FBK | Mortal Company", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

        // ── Top bar: close + status ──
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * uiScale, 6.0f * uiScale));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * uiScale, 4.0f * uiScale));

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.06f, 0.06f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.08f, 0.08f, 1.00f));
            if (ImGui::Button("—")) g_collapsed = true;
            ImGui::PopStyleColor(2);
            ImGui::SameLine();

            const char* dotOn = "●";
            ImGui::PushStyleColor(ImGuiCol_Text, g_il2cppReady ? ImVec4(0.2f,0.9f,0.2f,1) : ImVec4(0.9f,0.2f,0.2f,1));
            ImGui::Text("%s", dotOn); ImGui::SameLine();
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, g_mainHookOK ? ImVec4(0.2f,0.9f,0.2f,1) : ImVec4(0.9f,0.2f,0.2f,1));
            ImGui::Text("%s", dotOn); ImGui::SameLine();
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, g_inputOK ? ImVec4(0.2f,0.9f,0.2f,1) : ImVec4(0.9f,0.2f,0.2f,1));
            ImGui::Text("IN"); ImGui::SameLine();
            ImGui::PopStyleColor();
            ImGui::TextDisabled("v1.1");

            ImGui::PopStyleVar(2);
        }

        ImGui::Separator();

        // ── Tab bar ──
        if (ImGui::BeginTabBar("CheatTabs")) {
            if (ImGui::BeginTabItem("Movement")) {
                ImGui::Checkbox("Infinite Stamina",  &g_config.infiniteStamina);
                ImGui::Checkbox("Unlimited Sprint",  &g_config.unlimitedSprint);
                ImGui::Checkbox("Speed Hack",    &g_config.speedHack);
                ImGui::Checkbox("Super Jump",    &g_config.superJump);
                ImGui::Checkbox("Infinity Jumps",&g_config.infinityJumps);
                ImGui::Checkbox("No Gravity",    &g_config.noGravity);
                ImGui::Checkbox("Air Control",   &g_config.airControl);
                ImGui::Checkbox("No Fall Damage",&g_config.noFallDamage);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Player")) {
                ImGui::Checkbox("God Mode (Health)", &g_config.godMode);
                ImGui::Checkbox("Stamina Manipulation",&g_config.staminaManipulation);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Items")) {
                ImGui::Checkbox("Unlimited Flashlight", &g_config.unlimitedFlashlight);
                ImGui::Checkbox("Unlimited Usage",  &g_config.unlimitedUsage);
                ImGui::Checkbox("Infinite Jetpack Fuel",&g_config.infiniteJetpackFuel);
                ImGui::Checkbox("No Weight Limit",  &g_config.noWeightLimit);
                ImGui::Checkbox("Super Item Magnet",&g_config.superItemMagnet);
                ImGui::Checkbox("Auto Pickup",   &g_config.autoPickup);
                ImGui::Checkbox("Set Scrap Value",   &g_config.setScrapValue);
                ImGui::Checkbox("Auto Reward Ads",  &g_config.autoReward);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Combat")) {
                ImGui::Checkbox("One-Hit Kill Enemies", &g_config.oneHitKill);
                ImGui::Checkbox("Instant Kill All",  &g_config.instantKillAll);
                ImGui::Checkbox("Blind Enemies", &g_config.blindEnemies);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Economy")) {
                ImGui::Checkbox("Unlimited Money", &g_config.unlimitedMoney);
                ImGui::Checkbox("Free Items",  &g_config.freeItems);
                ImGui::Checkbox("Quota Manipulation", &g_config.quotaManipulation);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Traps")) {
                ImGui::Checkbox("Instant Doors", &g_config.instantDoors);
                ImGui::Checkbox("Disable Mines", &g_config.disableMines);
                ImGui::Checkbox("Ship Door Always Open",&g_config.shipDoorAlwaysOpen);
                ImGui::Checkbox("No Teleporter Cooldown",&g_config.noTeleporterCooldown);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("ESP")) {
                ImGui::Checkbox("Players", &g_config.espPlayers);
                if (g_config.espPlayers) {
                    ImGui::Indent(16.0f);
                    ImGui::Checkbox("Boxes + Health", &g_config.espBoxes);
                    ImGui::Checkbox("Tracelines", &g_config.espTracelines);
                    ImGui::Indent(-16.0f);
                }
                ImGui::Checkbox("Objects / Items", &g_config.espObjects);
                if (g_config.espPlayers || g_config.espObjects) {
                    ImGui::Checkbox("Labels (HP, dist, $)", &g_config.espLabels);
                    ImGui::SliderFloat("Max Distance", &g_config.espMaxDist, 5.0f, 100.0f, "%.0fm");
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * uiScale, 6.0f * uiScale));
        if (ImGui::Button("Save Config")) SaveConfig();
        ImGui::SameLine();
        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::PopStyleVar();

        ImGui::End();
    }

    // ── Render ──
    ImGui::Render();

    GLStateBackup glBackup;
    BackupGLState(glBackup);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    CheckGLError("after RenderDrawData");

    RestoreGLState(glBackup);

    return orig_eglSwapBuffers(dpy, surf);
}

// ══════════════════════════════════════════════════════════════════════
// INIT
// ══════════════════════════════════════════════════════════════════════

static void* worker(void*) {
    while (true) {
        void* handle = dlopen("libil2cpp.so", RTLD_NOLOAD);
        if (handle) {
            IL2CPP::Globals.m_GameAssembly = handle;
            LOGD("libil2cpp.so found - waiting for full init...");
            sleep(2);
            LOGD("proceeding with il2cpp init on main thread");
            break;
        }
        usleep(500000);
    }
    return nullptr;
}

__attribute__((constructor))
static void init() {
    LOGD("cheat .so loaded");

    // Init ESP shared data mutex
    pthread_mutex_init(&g_espData.mutex, nullptr);

    // ── Hook AInputQueue_finishEvent for always-on native touch ──
    void* libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (libandroid) {
        void* ainputTarget = dlsym(libandroid, "AInputQueue_finishEvent");
        if (ainputTarget) {
            hook_func(ainputTarget, (void*)my_AInputQueue_finishEvent, (void**)&orig_AInputQueue_finishEvent);
            LOGD("AInputQueue_finishEvent hooked — native touch active in all scenes");
        } else {
            LOGE("AInputQueue_finishEvent not found in libandroid.so");
        }
    } else {
        LOGE("libandroid.so not found — native touch unavailable");
    }

    // ── Hook eglSwapBuffers for render-thread UI ──
    void* egl = dlopen("libEGL.so", RTLD_NOW);
    void* target = egl ? dlsym(egl, "eglSwapBuffers") : nullptr;
    if (target) { hook_func(target, (void*)my_eglSwapBuffers, (void**)&orig_eglSwapBuffers); g_hookOK = true; }
    else LOGE("eglSwapBuffers not found");

    pthread_create(&g_worker, nullptr, worker, nullptr);
}
