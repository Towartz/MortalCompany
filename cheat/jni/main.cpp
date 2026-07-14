#include <jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <dlfcn.h>
#include <shadowhook.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
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
#define LOGD(...) __android_log_print(ANDROID_LOG_INFO, "FBK", __VA_ARGS__)
#endif
#ifndef LOGW
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "FBK", __VA_ARGS__)
#endif
#ifndef LOGE
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FBK", __VA_ARGS__)
#endif

// ── State ──
static bool g_imguiReady   = false;
static bool g_il2cppReady  = false;
static bool g_il2cppInitAttempted = false;
static bool g_hookOK       = false;
static bool g_inputOK      = false;  // true = at least one input hook is confirmed active
static bool g_mainHookOK   = false;
static bool g_displaySizeChanged = false;
// EGL surface dimensions updated every frame in my_eglSwapBuffers.
// Used by the Unity GetTouch hook to flip Y from bottom-left to top-left origin.
static volatile int g_physW = 0;
static volatile int g_physH = 0;
static double g_LastTime   = 0.0;
static pthread_t g_worker;

// ── Unity Input.GetTouch hook (primary touch source for Unity games) ──
typedef void (*Input_GetTouch_t)(void* ret, int index);
static Input_GetTouch_t orig_GetTouch = nullptr;
// g_getTouchHooked: hook_func() succeeded for GetTouch
// g_getTouchWorking: at least one actual event has been delivered through it
static bool g_getTouchHooked  = false;
static bool g_getTouchWorking = false;
static volatile bool g_wantCapture = false;
// AInputQueue hook confirmed installed
static bool g_ainputHooked = false;

// ── Thread-safe input event queue ──
// Events are captured on Unity's input thread (my_AInputQueue_finishEvent) and
// drained on the render thread (my_eglSwapBuffers, before ImGui::NewFrame). This
// keeps ALL ImGuiIO access on a single thread, eliminating the data race that
// occurred when ImGui_ImplAndroid_HandleInputEvent ran on the input thread.
struct InputEvent {
    int32_t type;          // AINPUT_EVENT_TYPE_MOTION / _KEY
    int32_t action;        // masked motion/key action
    int32_t pointerIndex;
    int32_t toolType;
    float   x, y;          // already in EGL surface space (top-left, y-down)
    int32_t buttonState;
    float   axisH, axisV;  // scroll axes
    bool    fromUnity;     // true = Unity GetTouch (y already flipped); false = AInputQueue
    int32_t keyCode;       // only for KEY events
    int32_t metaState;     // key modifier state (AMETA_*)
    int32_t scanCode;      // key scan code for SetKeyEventNativeData
    int32_t unicodeChar;   // unicode character for AddInputCharactersUTF8
};
static pthread_mutex_t g_inputMutex = PTHREAD_MUTEX_INITIALIZER;
static std::vector<InputEvent> g_inputQueue;

static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

// ══════════════════════════════════════════════════════════════════════
// NATIVE TOUCH — hooks AInputQueue_finishEvent. We ONLY extract the event
// fields here (input thread) and push them into a mutex-protected queue.
// The actual ImGui feeding happens on the render thread (see my_eglSwapBuffers)
// so ImGuiIO is touched by a single thread. Works in ANY scene.
// NOTE: we always forward to the original finishEvent — by the time this hook
// runs the game has already retrieved the event via AInputQueue_getEvent, so
// "eating" it here cannot block the game. True input blocking would require
// hooking AInputQueue_getEvent instead.
// ══════════════════════════════════════════════════════════════════════

typedef void (*AInputQueue_finishEvent_t)(void* queue, void* event, int handled);
static AInputQueue_finishEvent_t orig_AInputQueue_finishEvent = nullptr;
typedef int  (*AInputQueue_getEvent_t)(void* queue, void** outEvent);
static AInputQueue_getEvent_t   orig_AInputQueue_getEvent = nullptr;

// Extract an AInputEvent into our queue (motion + key). Called from whichever
// AInputQueue hook actually fires. Returns true if something was queued.
// AInputQueue coords are in window/surface space (top-left, y-down) so no flip needed.
static bool EnqueueAInputEvent(AInputEvent* ev) {
    if (!ev) return false;
    InputEvent ie{};
    ie.type = AInputEvent_getType(ev);
    ie.fromUnity = false;
    if (ie.type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t a = AMotionEvent_getAction(ev);
        ie.pointerIndex = (a & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        ie.action  = a & AMOTION_EVENT_ACTION_MASK;
        // For non-primary pointers: map POINTER_DOWN/UP to DOWN/UP and use
        // that pointer's own coordinates (not pointer 0's coordinates).
        int32_t coordIdx = ie.pointerIndex; // which pointer's coords to use
        if (ie.pointerIndex != 0) {
            if (ie.action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                ie.action = AMOTION_EVENT_ACTION_DOWN;
            } else if (ie.action == AMOTION_EVENT_ACTION_POINTER_UP) {
                ie.action = AMOTION_EVENT_ACTION_UP;
            } else {
                return false; // non-primary move events ignored
            }
        }
        // Use the correct pointer index for coordinates
        size_t count = AMotionEvent_getPointerCount(ev);
        if ((size_t)coordIdx >= count) coordIdx = 0;
        ie.toolType = AMotionEvent_getToolType(ev, coordIdx);
        ie.x = AMotionEvent_getX(ev, coordIdx);
        ie.y = AMotionEvent_getY(ev, coordIdx);
        ie.buttonState = AMotionEvent_getButtonState(ev);
        ie.axisH = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HSCROLL, coordIdx);
        ie.axisV = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_VSCROLL, coordIdx);
    } else if (ie.type == AINPUT_EVENT_TYPE_KEY) {
        ie.action    = AKeyEvent_getAction(ev);
        ie.keyCode   = AKeyEvent_getKeyCode(ev);
        ie.scanCode  = AKeyEvent_getScanCode(ev);
        ie.metaState = AKeyEvent_getMetaState(ev);
        // Capture unicode character for text input
        ie.unicodeChar = 0;
    } else {
        return false;
    }
    pthread_mutex_lock(&g_inputMutex);
    g_inputQueue.push_back(ie);
    pthread_mutex_unlock(&g_inputMutex);
    if (!g_inputOK) { g_inputOK = true; LOGD("first input event captured (source ok)"); }
    return true;
}

static void my_AInputQueue_finishEvent(void* queue, void* event, int handled) {
    // Only capture via AInputQueue when GetTouch is NOT yet delivering events.
    // Running both paths simultaneously double-feeds ImGui: WantCaptureMouse
    // becomes sticky, the GetTouch hook then sets every touch phase=Stationary,
    // and the game can never process input — causing a full freeze.
    // Once GetTouch is working (g_getTouchWorking=true) this path stays silent.
    if (event && g_imguiReady && !g_getTouchWorking)
        EnqueueAInputEvent((AInputEvent*)event);
    // Always forward — never disturb the game's input pipeline.
    if (orig_AInputQueue_finishEvent && is_valid_fn_ptr(orig_AInputQueue_finishEvent))
        orig_AInputQueue_finishEvent(queue, event, handled);
}

static int my_AInputQueue_getEvent(void* queue, void** outEvent) {
    // We capture in finishEvent (not here) to avoid double-enqueue.
    // This trampoline just forwards to the original.
    return (orig_AInputQueue_getEvent && is_valid_fn_ptr(orig_AInputQueue_getEvent)) ? orig_AInputQueue_getEvent(queue, outEvent) : 0;
}

extern "C" bool ImGui_ImplOpenGL3_LoadFunctions() { return true; }

// ══════════════════════════════════════════════════════════════════════
// MAIN-THREAD HOOK (FP_Controller::Update)
// ══════════════════════════════════════════════════════════════════════

typedef void (*FP_Update_t)(void*);
static FP_Update_t orig_FP_Update = nullptr;
static int g_mainUpdateCount = 0;

static void my_FP_Update(void* instance) {
    if (orig_FP_Update && is_valid_fn_ptr(orig_FP_Update))
        orig_FP_Update(instance);

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
// UNITY INPUT HOOK — UnityEngine.Input.GetTouch (primary touch source)
// ══════════════════════════════════════════════════════════════════════

static int g_touchPosOff = -1;
static int g_touchPhaseOff = -1;

// IL2CPP auto-properties back to m_<name>; resolve with fallback across the
// simple and fully-qualified class names.
static int ResolveTouchField(const char* base, const char* alt) {
    int off = GetFieldOffset("Touch", base);
    if (off < 0 && alt) off = GetFieldOffset("Touch", alt);
    if (off < 0) off = GetFieldOffset("UnityEngine.Touch", base);
    if (off < 0 && alt) off = GetFieldOffset("UnityEngine.Touch", alt);
    return off;
}



static void my_GetTouch(void* ret, int index) {
    // Guard: if orig_GetTouch is null or points into non-executable memory
    // (Dobby hook failed) skip the call — never execute a null trampoline.
    if (orig_GetTouch && is_valid_fn_ptr(orig_GetTouch))
        orig_GetTouch(ret, index);
    
    static int s_gtLog = 0;
    if (s_gtLog < 8) {
        s_gtLog++;
        __android_log_print(ANDROID_LOG_INFO, "FBK", "DIAG GetTouch called (idx=%d ret=%p posOff=%d phaseOff=%d imguiReady=%d)",
             index, ret, g_touchPosOff, g_touchPhaseOff, g_imguiReady ? 1 : 0);
    }
    if (!g_imguiReady || !ret) return;

    // Resolve Touch struct field offsets once (IL2CPP value-type fields).
    if (g_touchPosOff < 0)   g_touchPosOff   = ResolveTouchField("position", "m_position");
    if (g_touchPhaseOff < 0) g_touchPhaseOff = ResolveTouchField("phase", "m_phase");
    if (g_touchPosOff < 0) return;

    float px = *(float*)((uint8_t*)ret + g_touchPosOff);
    float py = *(float*)((uint8_t*)ret + g_touchPosOff + 4);
    int   phase = (g_touchPhaseOff >= 0) ? *(int*)((uint8_t*)ret + g_touchPhaseOff) : 0;

    // TouchPhase: 0=Began, 1=Moved, 2=Stationary, 3=Ended, 4=Canceled
    int32_t action;
    if (phase == 0)            action = AMOTION_EVENT_ACTION_DOWN;
    else if (phase == 3 || phase == 4) action = AMOTION_EVENT_ACTION_UP;
    else                       action = AMOTION_EVENT_ACTION_MOVE; // Moved / Stationary

    InputEvent ie{};
    ie.type      = AINPUT_EVENT_TYPE_MOTION;
    ie.action    = action;
    ie.toolType  = AMOTION_EVENT_TOOL_TYPE_FINGER;
    ie.fromUnity = true;
    ie.x = px;
    // Unity Touch.position is bottom-left origin (y-up); ImGui is top-left (y-down).
    // Use the stored EGL surface height for a correct flip. If it isn't available yet
    // fall back to py (no flip) — better than always producing 0.
    int ph = g_physH;
    ie.y = (ph > 0) ? ((float)ph - py) : py;

    // Only enqueue if ImGui would want this event OR it's a release (always enqueue
    // releases to avoid stuck-button state).
    // NOTE: We enqueue BEFORE setting Stationary so ImGui sees the real action.
    pthread_mutex_lock(&g_inputMutex);
    g_inputQueue.push_back(ie);
    pthread_mutex_unlock(&g_inputMutex);
    if (!g_inputOK) g_inputOK = true;
    g_getTouchWorking = true;

    // Block the game from seeing this touch while ImGui is capturing it.
    // Only suppress non-release phases; always let UP/Canceled through so
    // the game's touch state machine doesn't get stuck.
    if (g_touchPhaseOff >= 0 && g_wantCapture
            && phase != 3 && phase != 4) // 3=Ended, 4=Canceled
        *(int*)((uint8_t*)ret + g_touchPhaseOff) = 2; // TouchPhase::Stationary
}

static void SetupInputHooks() {
    // Already successfully hooked or permanently gave up — nothing to do.
    if (g_getTouchHooked) return;

    // Try to resolve UnityEngine.Input.GetTouch via IL2CPP.
    // We retry every frame (cheap) until it resolves or we give up.
    void* ptr = nullptr;
    if (IL2CPP::Globals.m_GameAssembly) {
        ptr = IL2CPP::Class::Utils::GetMethodPointer("UnityEngine.Input", "GetTouch", 1);
        if (!ptr)
            ptr = IL2CPP::Class::Utils::GetMethodPointer("Input", "GetTouch", 1);
    }

    if (ptr) {
        if (hook_func(ptr, (void*)my_GetTouch, (void**)&orig_GetTouch)) {
            g_getTouchHooked = true;
            if (!g_inputOK) { g_inputOK = true; }
            LOGD("Unity Input.GetTouch hooked — primary touch source active");
        } else {
            // hook_func failed — DobbyHook couldn't install it.
            // Limit retries to avoid hammering the hook system.
            static int s_hookRetries = 0;
            if (++s_hookRetries >= 3) {
                g_getTouchHooked = true;   // stop retrying — flag as "done"
                orig_GetTouch = nullptr;   // ensure we never call it
                LOGW("Unity Input.GetTouch hook failed after %d attempts — "
                     "falling back to AInputQueue only", s_hookRetries);
            } else {
                LOGW("Unity Input.GetTouch hook attempt %d failed — will retry", s_hookRetries);
            }
        }
    }
    // If ptr is null we keep retrying next frame — no pool slots consumed.
    // AInputQueue (g_ainputHooked) acts as unconditional fallback.
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
    // my_eglSwapBuffers only runs after il2cpp_init() has fully completed, so
    // calling IL2CPP::UnityAPI::Initialize() here is always safe. Retry on
    // failure instead of permanently giving up.
    if (!g_il2cppInitAttempted && IL2CPP::Globals.m_GameAssembly) {
        g_il2cppInitAttempted = true;
        if (IL2CPP::UnityAPI::Initialize()) {
            g_il2cppReady = true;
            LoadConfig();
            LOGD("il2cpp ready");
        } else {
            g_il2cppInitAttempted = false;
            LOGE("IL2CPP::UnityAPI::Initialize() failed — will retry next frame");
        }
    }

    // ── Set up main thread hook (retry each frame until FP_Controller is resolvable) ──
    if (g_il2cppReady && !g_mainHookOK) {
        TryHookMainThread();
    }

    // ── Set up Unity Input.GetTouch hook ──
    // Retry every frame regardless of g_il2cppReady — the class may become
    // available before the full IL2CPP init path completes. AInputQueue is
    // always active as a fallback (g_ainputHooked set in init()).
    if (!g_getTouchHooked) {
        SetupInputHooks();
    }

    // ── Mark INPUT green once a real event is received ──
    // We no longer pre-emptively set g_inputOK=true just because the hook is
    // installed; we wait for an actual event to confirm the path is working.
    // (g_inputOK is set to true inside EnqueueAInputEvent / my_GetTouch
    //  on first successful delivery.)

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
    // Publish for Unity GetTouch Y-flip (read on game thread).
    g_physW = physW;
    g_physH = physH;
    io.DisplaySize = ImVec2((float)physW, (float)physH);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // ── Keep UI a constant physical size across resolution changes ──
    // The game's graphics "resolution changer" resizes the EGL surface. If we
    // only set FontGlobalScale once (at init) the overlay shrinks/grows with the
    // internal render resolution. Recomputing it every frame keeps the UI the
    // same on-screen size regardless of the game's internal resolution.
    io.FontGlobalScale = fmaxf(1.0f, (float)physW / 1920.0f);

    // Track surface-size changes so we can re-center / clamp the window when the
    // game switches resolution (otherwise the window keeps its old pixel
    // position and ends up off-screen, e.g. bottom-right).
    {
        static EGLint s_lastW = -1, s_lastH = -1;
        g_displaySizeChanged = (physW != s_lastW || physH != s_lastH);
        s_lastW = physW;
        s_lastH = physH;
    }

    // ── Feed queued native input to ImGui (thread-safe) ──
    // Raw events were captured on Unity's input thread / AInputQueue and pushed
    // into g_inputQueue. Drained HERE on the render thread so ImGuiIO is only
    // ever touched from one thread (no race).
    //
    // Coordinate spaces:
    //   AInputQueue (fromUnity=false): already in EGL surface space (top-left, y-down).
    //     No scaling or flip required.
    //   Unity GetTouch (fromUnity=true): x in logical pixels (matches surface width),
    //     y already flipped to top-left in my_GetTouch using g_physH.
    //     No further transform required here.
    {
        std::vector<InputEvent> local;
        pthread_mutex_lock(&g_inputMutex);
        local.swap(g_inputQueue);
        pthread_mutex_unlock(&g_inputMutex);

        for (const InputEvent& ie : local) {
            if (ie.type == AINPUT_EVENT_TYPE_KEY) {
                // ── Full key event forwarding with modifier state ──
                bool down = (ie.action == AKEY_EVENT_ACTION_DOWN);

                // Forward modifier key state first (same pattern as imgui_impl_android)
                io.AddKeyEvent(ImGuiMod_Ctrl,  (ie.metaState & AMETA_CTRL_ON)  != 0);
                io.AddKeyEvent(ImGuiMod_Shift, (ie.metaState & AMETA_SHIFT_ON) != 0);
                io.AddKeyEvent(ImGuiMod_Alt,   (ie.metaState & AMETA_ALT_ON)   != 0);
                io.AddKeyEvent(ImGuiMod_Super, (ie.metaState & AMETA_META_ON)  != 0);

                // Map all relevant keycodes to ImGuiKey
                ImGuiKey key = ImGuiKey_None;
                switch (ie.keyCode) {
                    case AKEYCODE_BACK:          key = ImGuiKey_Escape;      break;
                    case AKEYCODE_ENTER:         key = ImGuiKey_Enter;       break;
                    case AKEYCODE_NUMPAD_ENTER:  key = ImGuiKey_KeypadEnter; break;
                    case AKEYCODE_DEL:           key = ImGuiKey_Backspace;   break;
                    case AKEYCODE_FORWARD_DEL:   key = ImGuiKey_Delete;      break;
                    case AKEYCODE_DPAD_LEFT:     key = ImGuiKey_LeftArrow;   break;
                    case AKEYCODE_DPAD_RIGHT:    key = ImGuiKey_RightArrow;  break;
                    case AKEYCODE_DPAD_UP:       key = ImGuiKey_UpArrow;     break;
                    case AKEYCODE_DPAD_DOWN:     key = ImGuiKey_DownArrow;   break;
                    case AKEYCODE_MOVE_HOME:     key = ImGuiKey_Home;        break;
                    case AKEYCODE_MOVE_END:      key = ImGuiKey_End;         break;
                    case AKEYCODE_PAGE_UP:       key = ImGuiKey_PageUp;      break;
                    case AKEYCODE_PAGE_DOWN:     key = ImGuiKey_PageDown;    break;
                    case AKEYCODE_INSERT:        key = ImGuiKey_Insert;      break;
                    case AKEYCODE_TAB:           key = ImGuiKey_Tab;         break;
                    case AKEYCODE_SPACE:         key = ImGuiKey_Space;       break;
                    case AKEYCODE_ESCAPE:        key = ImGuiKey_Escape;      break;
                    case AKEYCODE_CTRL_LEFT:     key = ImGuiKey_LeftCtrl;    break;
                    case AKEYCODE_CTRL_RIGHT:    key = ImGuiKey_RightCtrl;   break;
                    case AKEYCODE_SHIFT_LEFT:    key = ImGuiKey_LeftShift;   break;
                    case AKEYCODE_SHIFT_RIGHT:   key = ImGuiKey_RightShift;  break;
                    case AKEYCODE_ALT_LEFT:      key = ImGuiKey_LeftAlt;     break;
                    case AKEYCODE_ALT_RIGHT:     key = ImGuiKey_RightAlt;    break;
                    case AKEYCODE_A:             key = ImGuiKey_A;           break;
                    case AKEYCODE_C:             key = ImGuiKey_C;           break;
                    case AKEYCODE_V:             key = ImGuiKey_V;           break;
                    case AKEYCODE_X:             key = ImGuiKey_X;           break;
                    case AKEYCODE_Y:             key = ImGuiKey_Y;           break;
                    case AKEYCODE_Z:             key = ImGuiKey_Z;           break;
                    default: break;
                }
                if (key != ImGuiKey_None) {
                    io.AddKeyEvent(key, down);
                    io.SetKeyEventNativeData(key, ie.keyCode, ie.scanCode);
                }

                // Forward printable characters as text input (KEY_DOWN only)
                // This allows ImGui InputText widgets to receive typed characters.
                if (down && ie.unicodeChar >= 32 && ie.unicodeChar < 127) {
                    char buf[2] = { (char)ie.unicodeChar, 0 };
                    io.AddInputCharactersUTF8(buf);
                }
                continue;
            }

            if (ie.type != AINPUT_EVENT_TYPE_MOTION) continue;

            // Coords are in EGL surface space — use them directly.
            float fx = ie.x;
            float fy = ie.y;

            // Clamp to surface bounds to avoid feeding out-of-range positions
            // (e.g. if the Unity screen is bigger than the EGL render surface).
            if (fx < 0.0f) fx = 0.0f; else if (fx > (float)physW) fx = (float)physW;
            if (fy < 0.0f) fy = 0.0f; else if (fy > (float)physH) fy = (float)physH;

            switch (ie.toolType) {
                case AMOTION_EVENT_TOOL_TYPE_MOUSE:
                    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse); break;
                case AMOTION_EVENT_TOOL_TYPE_STYLUS:
                case AMOTION_EVENT_TOOL_TYPE_ERASER:
                    io.AddMouseSourceEvent(ImGuiMouseSource_Pen); break;
                default:
                    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen); break;
            }
            switch (ie.action) {
                case AMOTION_EVENT_ACTION_DOWN:
                    io.AddMousePosEvent(fx, fy);
                    io.AddMouseButtonEvent(0, true);
                    break;
                case AMOTION_EVENT_ACTION_UP:
                case AMOTION_EVENT_ACTION_CANCEL:
                    io.AddMousePosEvent(fx, fy);
                    io.AddMouseButtonEvent(0, false);
                    // Reset cursor position so ImGui doesn't leave hover-state
                    // on the last-touched widget after finger lifts.
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                    break;
                case AMOTION_EVENT_ACTION_BUTTON_PRESS:
                case AMOTION_EVENT_ACTION_BUTTON_RELEASE: {
                    // Always send position before button state for physical mice.
                    io.AddMousePosEvent(fx, fy);
                    io.AddMouseButtonEvent(0, (ie.buttonState & AMOTION_EVENT_BUTTON_PRIMARY)   != 0);
                    io.AddMouseButtonEvent(1, (ie.buttonState & AMOTION_EVENT_BUTTON_SECONDARY) != 0);
                    io.AddMouseButtonEvent(2, (ie.buttonState & AMOTION_EVENT_BUTTON_TERTIARY)  != 0);
                    break;
                }
                case AMOTION_EVENT_ACTION_HOVER_MOVE:
                case AMOTION_EVENT_ACTION_MOVE:
                    io.AddMousePosEvent(fx, fy);
                    break;
                case AMOTION_EVENT_ACTION_SCROLL:
                    // Always update position before scroll so the right window scrolls.
                    io.AddMousePosEvent(fx, fy);
                    io.AddMouseWheelEvent(ie.axisH, ie.axisV);
                    break;
                case AMOTION_EVENT_ACTION_OUTSIDE:
                case AMOTION_EVENT_ACTION_HOVER_ENTER:
                case AMOTION_EVENT_ACTION_HOVER_EXIT:
                    io.AddMousePosEvent(fx, fy);
                    break;
            }
        }
    }

    // ── New Frame ──
    // NOTE: We deliberately do NOT call ImGui_ImplAndroid_NewFrame() here. That
    // backend function dereferences g_Window (set only by ImGui_ImplAndroid_Init),
    // which we never call because this is an injected overlay with no
    // ANativeWindow handle — calling it crashes with SIGSEGV (null deref).
    // Instead we drive io.DisplaySize / DisplayFramebufferScale from eglQuerySurface
    // (above) and io.DeltaTime (above), which is all the Android NewFrame did anyway.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    // Publish current capture state for the Unity GetTouch hook (reads it on the
    // game thread to decide whether to block the touch from the game).
    g_wantCapture = io.WantCaptureMouse;

    // ── Gesture processing: touch-drag-to-scroll + long-press → right-click ──
    // Runs AFTER ImGui::NewFrame() so io.MouseDown / io.MouseDownDuration are
    // fully up to date for this frame (MouseDownDuration is only computed inside
    // NewFrame). Synthesized events apply on the next frame (1-frame latency).
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
        // io.MouseDownDuration[0] is valid now (computed during NewFrame above).
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

    // ── ESP (rendered from main-thread-gathered shared buffer, no Unity APIs here) ──
    // Must be called after ImGui::NewFrame() so GetBackgroundDrawList() is valid.
    RenderESPRenderThread();

    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    float uiScale = fmaxf(1.0f, sw / 1920.0f);

    // ── Main UI ──
    ImGui::SetNextWindowPos(ImVec2(sw * 0.5f, sh * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::Begin("FBK | Mortal Company", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

    // Start expanded by default (first frame only) — user can collapse freely.
    {
        static bool s_collapsedInit = false;
        if (!s_collapsedInit) {
            ImGui::SetWindowCollapsed(false, ImGuiCond_Always);
            s_collapsedInit = true;
        }
    }

    // ── Re-center on resolution change + keep window on-screen ──
    // When the game's resolution changer resizes the EGL surface, the window's
    // old pixel position becomes invalid (ends up off-screen, e.g. bottom-right).
    // Re-center it once when the size changes, then clamp every frame so it can
    // never drift outside the viewport.
    if (g_displaySizeChanged) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        float maxX = fmaxf(0.0f, io.DisplaySize.x - size.x);
        float maxY = fmaxf(0.0f, io.DisplaySize.y - size.y);
        float nx = (pos.x < 0.0f) ? 0.0f : (pos.x > maxX ? maxX : pos.x);
        float ny = (pos.y < 0.0f) ? 0.0f : (pos.y > maxY ? maxY : pos.y);
        if (nx != pos.x || ny != pos.y)
            ImGui::SetWindowPos(ImVec2(nx, ny), ImGuiCond_Always);
    }

    // ── Status indicators: IL2CPP | HOOK | INPUT ──
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * uiScale, 4.0f * uiScale));

        ImGui::PushStyleColor(ImGuiCol_Text, g_il2cppReady ? ImVec4(0.2f,0.9f,0.2f,1.0f) : ImVec4(0.9f,0.2f,0.2f,1.0f));
        ImGui::Text("IL2CPP"); ImGui::SameLine();
        ImGui::PopStyleColor();

        ImGui::TextDisabled("|"); ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Text, g_mainHookOK ? ImVec4(0.2f,0.9f,0.2f,1.0f) : ImVec4(0.9f,0.2f,0.2f,1.0f));
        ImGui::Text("HOOK"); ImGui::SameLine();
        ImGui::PopStyleColor();

        ImGui::TextDisabled("|"); ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Text, g_inputOK ? ImVec4(0.2f,0.9f,0.2f,1.0f) : ImVec4(0.9f,0.2f,0.2f,1.0f));
        ImGui::Text("INPUT"); ImGui::SameLine();
        ImGui::PopStyleColor();

        ImGui::TextDisabled("v1.1");

        ImGui::PopStyleVar();
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

    // ── Render ──
    ImGui::Render();

    GLStateBackup glBackup;
    BackupGLState(glBackup);

    // Set viewport explicitly to full surface size before ImGui rendering.
    // The game may have left a reduced viewport (e.g. split-screen, UI pass),
    // which would clip our overlay to a fraction of the screen.
    glViewport(0, 0, physW, physH);
    glScissor(0, 0, physW, physH);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    CheckGLError("after RenderDrawData");

    RestoreGLState(glBackup);

    return (orig_eglSwapBuffers && is_valid_fn_ptr(orig_eglSwapBuffers)) ? orig_eglSwapBuffers(dpy, surf) : EGL_FALSE;
}

// ══════════════════════════════════════════════════════════════════════
// INIT
// ══════════════════════════════════════════════════════════════════════

static void* worker(void*) {
    while (true) {
        void* handle = dlopen("libil2cpp.so", RTLD_NOLOAD);
        if (handle) {
            LOGD("libil2cpp.so mapped — il2cpp_init running on main thread");

            // IMPORTANT: Do NOT call any IL2CPP runtime API here (no
            // il2cpp_domain_get / il2cpp_domain_get_assemblies). The main thread
            // is still inside il2cpp_init(); touching IL2CPP's internal state from
            // this worker thread concurrently races with that initialization and
            // aborts the process with SIGABRT during il2cpp_init.
            //
            // We only record the handle. The real IL2CPP::UnityAPI::Initialize()
            // is performed later from my_eglSwapBuffers(), which can only run
            // AFTER il2cpp_init has fully completed (rendering never starts
            // before IL2CPP is up), so it is completely safe.
            IL2CPP::Globals.m_GameAssembly = handle;
            LOGD("libil2cpp handle cached — il2cpp init deferred to render thread");
            break;
        }
        usleep(500000);
    }
    return nullptr;
}

__attribute__((constructor))
static void init() {
    LOGD("cheat .so loaded");
    
    shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);

    // Init ESP shared data mutex
    pthread_mutex_init(&g_espData.mutex, nullptr);

    // ── Hook AInputQueue input retrieval for native touch ──
    // We hook BOTH getEvent (universal retrieval point — every input consumer
    // must call it) and finishEvent (release point). getEvent is the reliable
    // one; finishEvent is a secondary capture. Hook-install results are logged
    // so failures are visible in logcat.
    void* libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (libandroid) {
        // Hook getEvent as a trampoline (capture happens in finishEvent).
        void* getEventTarget = dlsym(libandroid, "AInputQueue_getEvent");
        if (getEventTarget) {
            if (hook_func(getEventTarget, (void*)my_AInputQueue_getEvent, (void**)&orig_AInputQueue_getEvent))
                LOGD("AInputQueue_getEvent hooked (trampoline)");
            else
                LOGE("AInputQueue_getEvent hook FAILED");
        } else {
            LOGE("AInputQueue_getEvent not found in libandroid.so");
        }

        // finishEvent is our primary AInputQueue capture point.
        void* ainputTarget = dlsym(libandroid, "AInputQueue_finishEvent");
        if (ainputTarget) {
            if (hook_func(ainputTarget, (void*)my_AInputQueue_finishEvent, (void**)&orig_AInputQueue_finishEvent)) {
                g_ainputHooked = true;
                LOGD("AInputQueue_finishEvent hooked — native touch fallback active");
            } else {
                LOGE("AInputQueue_finishEvent hook FAILED");
            }
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
