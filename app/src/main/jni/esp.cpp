#include "esp_data.h"
#include "esp.h"
#include "il2cpp.h"
#include "config.h"
#include "Includes/obfuscate.h"
#include "Includes/Logger.h"
#include <GLES3/gl3.h>
#include <cstring>
#include <cmath>
#include <atomic>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ucontext.h>
#include <sys/syscall.h>
#include <cstdio>
#include <cstdlib>

// Portable thread id (gettid() isn't declared by every NDK revision).
static pid_t fbk_gettid() {
#if defined(__NR_gettid)
    return (pid_t)syscall(__NR_gettid);
#else
    return (pid_t)getpid();
#endif
}

// ── Shared data (defined in esp_data.h) ──
ESPSharedData g_espData;

// ── Crash guard (still useful for risky offsets) ──
// NOTE: a SIGSEGV/SIGBUS outside a CRASH_GUARD block is a REAL crash. The handler
// must always surface it (logcat + trace file + chain to the OS crash reporter) so
// the process dies with a tombstone instead of vanishing "without trace".
thread_local sigjmp_buf g_crashJmpBuf;
thread_local volatile bool g_crashGuardActive = false;
static bool g_crashGuardInstalled = false;
static struct sigaction g_oldSevSA = {};   // previous SIGSEGV handler (Android's debuggerd)
static struct sigaction g_oldBusSA = {};   // previous SIGBUS handler

// Forward a signal to whatever handler was installed before us, so the system
// crash reporter still produces a tombstone for genuine faults.
static void ChainToPreviousHandler(int sig, siginfo_t* info, void* context) {
    struct sigaction* old = (sig == SIGSEGV) ? &g_oldSevSA : &g_oldBusSA;
    if ((old->sa_flags & SA_SIGINFO) && old->sa_sigaction) {
        old->sa_sigaction(sig, info, context);
        return; // should not return
    }
    if (old->sa_handler == SIG_DFL || old->sa_handler == SIG_IGN ||
        old->sa_handler == SIG_ERR || old->sa_handler == nullptr) {
        // No meaningful previous handler: restore default action and re-raise so
        // the kernel/debuggerd turns this into a real, reportable crash.
        signal(sig, SIG_DFL);
        raise(sig);
        abort(); // raise() should terminate; if it ever returns, force it.
    }
    // Plain (pre-SIGINFO) handler.
    old->sa_handler(sig);
    abort();
}

static void CrashHandler(int sig, siginfo_t* info, void* context) {
    // Inside a CRASH_GUARD block on THIS thread: recover silently as intended.
    if (g_crashGuardActive)
        siglongjmp(g_crashJmpBuf, 1);

    // ── Real crash: NEVER swallow it silently. ──
    uintptr_t addr = info ? (uintptr_t)info->si_addr : 0;
    uintptr_t pc = 0;
#if defined(__aarch64__)
    if (context) pc = (uintptr_t)((ucontext_t*)context)->uc_mcontext.pc;
#else
    if (context) pc = (uintptr_t)((ucontext_t*)context)->uc_mcontext.arm_pc;
#endif
    __android_log_print(ANDROID_LOG_ERROR, "FBK",
        "FATAL SIGNAL %d (si_addr=%p, pc=%p, tid=%d) — real crash, NOT recovered. "
        "See /sdcard/fbk_crash.txt and logcat for details.",
        sig, (void*)addr, (void*)pc, (int)fbk_gettid());

    // Minimal async-signal-safe trace file (offline debugging aid).
    int fd = open("/sdcard/fbk_crash.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        char buf[256];
        int n = snprintf(buf, sizeof(buf),
            "FATAL SIGNAL %d si_addr=%p pc=%p tid=%d\n", sig, (void*)addr, (void*)pc, (int)fbk_gettid());
        if (n > 0) write(fd, buf, (size_t)n);
        close(fd);
    }

    ChainToPreviousHandler(sig, info, context);
}

void InstallCrashGuard() {
    if (g_crashGuardInstalled) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sa.sa_sigaction = CrashHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_oldSevSA);
    sigaction(SIGBUS, &sa, &g_oldBusSA);
    g_crashGuardInstalled = true;
}

// ── Viewport size – written by render thread, read by main thread ──
static std::atomic<int> g_espCamWidth{1920};
static std::atomic<int> g_espCamHeight{1080};

// ── External refresh trigger ──
static std::atomic<bool> g_espRefreshRequested{false};

// ── Initialisation / cleanup ──
void ESPInit() {
    pthread_mutex_init(&g_espData.mutex, nullptr);
    InstallCrashGuard();
    LOGI("ESP system initialised");
}

void ESPShutdown() {
    pthread_mutex_destroy(&g_espData.mutex);
}

// ── World‑to‑screen using the real viewport size ──
static bool WorldToScreen(Unity::Vector3 wpos, float& sx, float& sy) {
    Unity::CCamera* cam = Unity::Camera::GetMain();
    if (!cam) return false;
    Unity::Vector3 s;
    cam->WorldToScreen(wpos, s);
    if (s.Z < 0.01f) return false;

    int w = g_espCamWidth.load(std::memory_order_relaxed);
    int h = g_espCamHeight.load(std::memory_order_relaxed);
    // s.X is already in screen coordinates (0..width), Y is from top (0..height)
    sx = s.X;
    sy = (float)h - s.Y;   // flip Y to bottom‑left origin
    return true;
}

Unity::Vector3 GetPos(void* transform) {
    Unity::Vector3 pos{0,0,0};
    if (!transform) return pos;
    CRASH_GUARD({
        pos = reinterpret_cast<Unity::CTransform*>(transform)->GetPosition();
    });
    return pos;
}

void SetPos(void* transform, Unity::Vector3 p) {
    if (!transform) return;
    CRASH_GUARD({
        reinterpret_cast<Unity::CTransform*>(transform)->SetPosition(p);
    });
}

void* GetTransformOf(void* obj) {
    if (!obj) return nullptr;
    void* tr = nullptr;
    CRASH_GUARD({
        tr = reinterpret_cast<Unity::CComponent*>(obj)->GetTransform();
    });
    return tr;
}

// ── Main‑thread gather (call every frame from your hooked Update) ──
void GatherESPData() {
    // Static state persists across calls
    static bool s_prevCamHadTransform = false;
    static int  s_transitionCooldown = 0;
    static int  s_camCheckCount = 0;
    static int  s_frameCounter = 0;

    s_frameCounter++;

    // Honour external refresh request immediately
    if (g_espRefreshRequested.exchange(false)) {
        s_transitionCooldown = 0;
        s_prevCamHadTransform = false;
        s_frameCounter = 0;
    }

    // ── Camera null detection ──
    if (s_prevCamHadTransform) {
        Unity::CCamera* checkCam = Unity::Camera::GetMain();
        if (!checkCam || !reinterpret_cast<Unity::CComponent*>(checkCam)->GetTransform()) {
            s_prevCamHadTransform = false;
            LOGI("ESP gather: camera lost");
        }
    }

    // ── Scene‑change detection ──
    if (!s_prevCamHadTransform) {
        Unity::CCamera* testCam = Unity::Camera::GetMain();
        if (testCam) {
            Unity::CTransform* testTx = reinterpret_cast<Unity::CComponent*>(testCam)->GetTransform();
            if (testTx) {
                s_prevCamHadTransform = true;
                s_transitionCooldown = 5;   // wait a few frames
                LOGI("ESP gather: camera found, cooldown=%d", s_transitionCooldown);
            }
        }
        if (++s_camCheckCount % 100 == 0)
            LOGI("ESP gather: waiting for camera... (check #%d)", s_camCheckCount);
    }

    // ── GC / transition cooldown ──
    if (s_transitionCooldown > 0) {
        s_transitionCooldown--;
        if (s_transitionCooldown == 0) {
            s_prevCamHadTransform = true;   // confirmed
            s_frameCounter = 0;             // force gather next frame
        }
        return;   // wait for next frame
    }

    // ── Only gather every 3rd frame (≈50ms @ 60 FPS) ──
    if (s_frameCounter % 3 != 0)
        return;

    // ── Actual data gathering (all Unity calls on main thread) ──
    if (!IL2CPP::Globals.m_GameAssembly) return;

    Unity::CCamera* cam = Unity::Camera::GetMain();
    if (!cam) return;
    Unity::CTransform* camTr = reinterpret_cast<Unity::CComponent*>(cam)->GetTransform();
    if (!camTr) return;
    Unity::Vector3 camPos = GetPos(camTr);

    float maxDistSq = g_config.espMaxDist * g_config.espMaxDist;

    // Lock the shared buffer only while writing
    pthread_mutex_lock(&g_espData.mutex);
    g_espData.playerCount = 0;
    g_espData.itemCount = 0;
    g_espData.dataReady = false;

    if (g_config.espPlayers) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("PlayerHealth");
        if (arr) {
            void* localPh = (arr->m_uMaxLength > 0) ? arr->At(0) : nullptr;
            for (uintptr_t i = 0; i < arr->m_uMaxLength && g_espData.playerCount < MAX_ESP_PLAYERS; i++) {
                void* ph = arr->At(i);
                if (!ph || ph == localPh) continue;

                Unity::CComponent* comp = reinterpret_cast<Unity::CComponent*>(ph);
                Unity::CTransform* tr = comp->GetTransform();
                if (!tr) continue;
                Unity::Vector3 wpos = GetPos(tr);
                float dx = wpos.X - camPos.X, dy = wpos.Y - camPos.Y, dz = wpos.Z - camPos.Z;
                float distSq = dx*dx + dy*dy + dz*dz;
                if (distSq > maxDistSq || distSq < 0.1f) continue;

                float sx, sy;
                if (!WorldToScreen(wpos, sx, sy)) continue;

                float hp = 100.0f;
                CRASH_GUARD({
                    int off = GetFieldOffset("Health", "healthPoints");
                    if (off >= 0) hp = AsClass(ph)->GetMemberValue<float>(off);
                });

                auto& p = g_espData.players[g_espData.playerCount++];
                p.sx = sx; p.sy = sy;
                p.dist = std::sqrt(distSq);
                p.hp = hp;
            }
        }
    }

    if (g_config.espObjects) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("InventoryItem", true);
        if (!arr)
            arr = Unity::Object::FindObjectsOfType<void>("GrabbableObject", true);
        if (arr) {
            LOGD("ESP gather: found %u InventoryItem/GrabbableObject", (unsigned)arr->m_uMaxLength);
            for (uintptr_t i = 0; i < arr->m_uMaxLength && g_espData.itemCount < MAX_ESP_ITEMS; i++) {
                void* item = arr->At(i);
                if (!item) continue;

                Unity::CComponent* comp = reinterpret_cast<Unity::CComponent*>(item);
                Unity::CTransform* tr = comp->GetTransform();
                if (!tr) continue;
                Unity::Vector3 wpos = GetPos(tr);

                float dx = wpos.X - camPos.X, dy = wpos.Y - camPos.Y, dz = wpos.Z - camPos.Z;
                float distSq = dx*dx + dy*dy + dz*dz;
                if (distSq > maxDistSq || distSq < 0.1f) continue;

                float sx, sy;
                if (!WorldToScreen(wpos, sx, sy)) continue;

                int val = 0; float wt = 0; bool heavy = false; char name[28] = {0};
                CRASH_GUARD({
                    int vOff = GetFieldOffset("InventoryItem", "Value");
                    if (vOff >= 0) val = AsClass(item)->GetMemberValue<int>(vOff);
                });
                CRASH_GUARD({
                    int wOff = GetFieldOffset("InventoryItem", "Weight");
                    if (wOff >= 0) wt = AsClass(item)->GetMemberValue<float>(wOff);
                });
                CRASH_GUARD({
                    int hOff = GetFieldOffset("InventoryItem", "isHeavy");
                    if (hOff >= 0) heavy = AsClass(item)->GetMemberValue<bool>(hOff);
                });
                // Read the item's display name so we can label it (itemName @0xC0 in dump.cs).
                CRASH_GUARD({
                    int nameOff = GetFieldOffset("InventoryItem", "itemName");
                    if (nameOff >= 0) {
                        Unity::System_String* s = AsClass(item)->GetMemberValue<Unity::System_String*>(nameOff);
                        if (s) {
                            std::string ns = s->ToString();
                            if (!ns.empty()) {
                                size_t n = ns.size() < sizeof(name) - 1 ? ns.size() : sizeof(name) - 1;
                                memcpy(name, ns.c_str(), n);
                                name[n] = '\0';
                            }
                        }
                    }
                });

                // Valuables-only mode: skip low-value junk so high-credit scrap stands out.
                if (g_config.espValuablesOnly && val < 20) continue;

                auto& e = g_espData.items[g_espData.itemCount++];
                e.sx = sx; e.sy = sy;
                e.dist = std::sqrt(distSq);
                e.value = val;
                e.weight = wt;
                e.isHeavy = heavy;
                memcpy(e.name, name, sizeof(e.name));
            }
        }
    }

    g_espData.drawPlayers   = g_config.espPlayers;
    g_espData.drawObjects   = g_config.espObjects;
    g_espData.drawBoxes     = g_config.espBoxes;
    g_espData.drawTracelines= g_config.espTracelines;
    g_espData.drawLabels    = g_config.espLabels;
    g_espData.showNames     = g_config.espShowNames;
    g_espData.valuablesOnly = g_config.espValuablesOnly;
    g_espData.maxDist       = g_config.espMaxDist;
    g_espData.dataReady     = true;
    pthread_mutex_unlock(&g_espData.mutex);

    LOGI("ESP gather: players=%d items=%d", g_espData.playerCount, g_espData.itemCount);
}

// ── External trigger (can be called from any thread) ──
void TriggerESPRefresh() {
    g_espRefreshRequested.store(true);
}

// ── OpenGL rendering (call from your GL injection / OnPostRender hook) ──
static GLuint g_espVBO = 0, g_espVAO = 0;
static const char* g_espVertSrc = R"(
#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform vec2 uScreen;
void main() {
    vec2 clip = (aPos / uScreen) * 2.0 - 1.0;
    clip.y = -clip.y;
    gl_Position = vec4(clip, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* g_espFragSrc = R"(
#version 300 es
precision mediump float;
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

static GLuint g_espProg = 0;
static GLint g_espScreenLoc = -1;

static void EnsureESPProgram() {
    if (g_espProg) return;

    GLint status;

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &g_espVertSrc, nullptr);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
    if (!status) { glDeleteShader(vs); return; }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &g_espFragSrc, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
    if (!status) { glDeleteShader(vs); glDeleteShader(fs); return; }

    g_espProg = glCreateProgram();
    glAttachShader(g_espProg, vs);
    glAttachShader(g_espProg, fs);
    glLinkProgram(g_espProg);
    glGetProgramiv(g_espProg, GL_LINK_STATUS, &status);
    if (!status) {
        glDeleteProgram(g_espProg); g_espProg = 0;
        glDeleteShader(vs); glDeleteShader(fs);
        return;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    g_espScreenLoc = glGetUniformLocation(g_espProg, "uScreen");
    glGenVertexArrays(1, &g_espVAO);
    glGenBuffers(1, &g_espVBO);
}

static void DrawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a, float screenW, float screenH) {
    float verts[] = { x1, y1, r, g, b, a, x2, y2, r, g, b, a };
    glBindVertexArray(g_espVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_espVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 24, (void*)8);
    glUniform2f(g_espScreenLoc, screenW, screenH);
    glDrawArrays(GL_LINES, 0, 2);
}

static void DrawRect(float cx, float cy, float hw, float hh, float r, float g, float b, float a, float sw, float sh) {
    float x1 = cx - hw, y1 = cy - hh, x2 = cx + hw, y2 = cy + hh;
    DrawLine(x1, y1, x2, y1, r, g, b, a, sw, sh);
    DrawLine(x2, y1, x2, y2, r, g, b, a, sw, sh);
    DrawLine(x2, y2, x1, y2, r, g, b, a, sw, sh);
    DrawLine(x1, y2, x1, y1, r, g, b, a, sw, sh);
}

// Minimal 3x5 monospace line-font so ESP labels/names actually render.
// Each glyph is 5 rows of 3 bits (bit2=left, bit1=mid, bit0=right).
static const uint16_t g_espFont[128] = {
    [0 ... 31]  = 0,            // control chars
    [' '] = 0,
    ['0'] = 0b111'101'101'101'101'111,
    ['1'] = 0b010'110'010'010'010'111,
    ['2'] = 0b111'001'111'100'111'111,
    ['3'] = 0b111'001'111'001'111'111,
    ['4'] = 0b101'101'111'001'001'001,
    ['5'] = 0b111'100'111'001'111'111,
    ['6'] = 0b111'100'111'101'101'111,
    ['7'] = 0b111'001'010'010'010'010,
    ['8'] = 0b111'101'111'101'101'111,
    ['9'] = 0b111'101'111'001'111'111,
    ['$'] = 0b010'111'111'111'111'010,  // crude dollar sign
    ['%'] = 0b101'001'010'100'101'000,
    ['-'] = 0b000'000'111'000'000'000,
    [':'] = 0b000'010'000'010'000'000,
    ['.'] = 0b000'000'000'000'000'010,
    ['/'] = 0b001'001'010'100'100'100,
    ['m'] = 0b000'111'101'111'101'101,
    ['M'] = 0b101'111'101'101'101'101,
    ['a'] = 0b000'000'111'101'111'111,
    ['A'] = 0b111'101'111'101'101'111,
    ['b'] = 0b100'100'110'101'101'111,
    ['B'] = 0b110'101'110'101'101'110,
    ['c'] = 0b000'000'110'100'100'110,
    ['C'] = 0b011'100'100'100'100'011,
    ['d'] = 0b001'001'011'101'101'111,
    ['D'] = 0b110'101'101'101'101'110,
    ['e'] = 0b000'111'100'111'100'111,
    ['E'] = 0b111'100'111'100'100'111,
    ['f'] = 0b011'010'110'010'010'010,
    ['F'] = 0b111'100'111'100'100'100,
    ['g'] = 0b000'011'101'111'011'111,
    ['G'] = 0b011'100'100'101'101'111,
    ['h'] = 0b100'100'110'101'101'101,
    ['H'] = 0b101'101'111'101'101'101,
    ['i'] = 0b010'000'010'010'010'010,
    ['I'] = 0b111'010'010'010'010'111,
    ['k'] = 0b100'100'101'110'101'101,
    ['K'] = 0b101'101'110'101'101'101,
    ['l'] = 0b010'010'010'010'010'010,
    ['L'] = 0b100'100'100'100'100'111,
    ['n'] = 0b000'000'110'101'101'101,
    ['N'] = 0b000'101'111'111'111'101,
    ['o'] = 0b000'000'110'101'101'110,
    ['O'] = 0b011'101'101'101'101'011,
    ['p'] = 0b000'110'101'110'100'100,
    ['P'] = 0b110'101'110'100'100'100,
    ['r'] = 0b000'000'110'100'100'100,
    ['R'] = 0b110'101'110'101'101'101,
    ['s'] = 0b000'000'111'001'110'111,
    ['S'] = 0b111'100'111'001'001'111,
    ['t'] = 0b010'110'010'010'010'001,
    ['T'] = 0b111'010'010'010'010'010,
    ['u'] = 0b000'000'101'101'101'111,
    ['U'] = 0b101'101'101'101'101'011,
    ['v'] = 0b000'000'101'101'101'010,
    ['V'] = 0b000'000'101'101'101'010,
    ['w'] = 0b000'000'101'101'111'101,
    ['W'] = 0b000'101'101'111'111'101,
    ['x'] = 0b000'000'101'010'101'101,
    ['X'] = 0b000'101'101'010'101'101,
    ['y'] = 0b000'000'101'101'111'011,
    ['Y'] = 0b000'101'101'010'010'010,
    ['z'] = 0b000'000'111'010'001'111,
    ['Z'] = 0b000'111'010'010'001'111,
};

static void DrawGlyph(uint16_t g, float x, float y, float s, float r, float gg, float b, float a, float sw, float sh) {
    for (int row = 0; row < 5; row++) {
        uint16_t bits = (g >> (row * 3)) & 0x7;
        if (bits & 0b100) DrawLine(x,        y + row * s, x + s,        y + row * s, r, gg, b, a, sw, sh);
        if (bits & 0b010) DrawLine(x + s,    y + row * s, x + 2 * s,    y + row * s, r, gg, b, a, sw, sh);
        if (bits & 0b001) DrawLine(x + 2 * s, y + row * s, x + 3 * s,    y + row * s, r, gg, b, a, sw, sh);
    }
}

// Draw a C-string with the 3x5 font. Returns the width drawn.
static float DrawString(float x, float y, const char* text, float r, float g, float b, float a, float sw, float sh) {
    if (!text) return 0.0f;
    float s = 2.0f;          // glyph pixel size
    float step = 4.0f * s;   // 3px glyph + 1px spacing
    float cx = x;
    for (const char* p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 128) c = 0;
        uint16_t gl = g_espFont[c];
        if (gl) DrawGlyph(gl, cx, y, s, r, g, b, a, sw, sh);
        cx += step;
    }
    return cx - x;
}

void RenderESPGLES() {
    if (!g_espData.dataReady) return;

    pthread_mutex_lock(&g_espData.mutex);

    EnsureESPProgram();
    glUseProgram(g_espProg);

    GLint prevBlendSrc, prevBlendDst, prevProg, prevVao, prevVbo;
    GLint prevTexture2D, prevActiveTexture, prevUnpackAlignment, prevSamplerBinding;
    GLint vp[4];
    GLboolean prevBlendEnabled = glIsEnabled(GL_BLEND);
    GLboolean prevDepthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevCullEnabled = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2D);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    glGetIntegerv(GL_SAMPLER_BINDING, &prevSamplerBinding);

    // The game may have left depth/scissor/cull on; an overlay drawn at clip-z 0
    // would otherwise be depth-culled or clipped away, so disable them for the draw.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float sw = (float)vp[2];
    float sh = (float)vp[3];

    g_espCamWidth.store(vp[2], std::memory_order_relaxed);
    g_espCamHeight.store(vp[3], std::memory_order_relaxed);

    // ── ESP diagnostic overlay ──
    {
        static int s_renderFrame = 0;
        s_renderFrame++;
        char dbg[96];
        snprintf(dbg, sizeof(dbg), "ESP [f=%d] players=%d items=%d",
                 s_renderFrame, g_espData.playerCount, g_espData.itemCount);
        DrawString(sw * 0.5f, 4, dbg, 0.0f, 1.0f, 0.0f, 0.9f, sw, sh);
    }

    // ── Players ──
    if (g_espData.drawPlayers) {
        for (int i = 0; i < g_espData.playerCount; i++) {
            auto& p = g_espData.players[i];
            float scale = 50.0f / (p.dist * 0.5f + 1.0f);
            float hw = 15.0f * scale;
            float hh = 40.0f * scale;

            if (g_espData.drawBoxes) {
                float hpColor = p.hp > 50.0f ? 0.0f : (p.hp > 25.0f ? 1.0f : 0.0f);
                DrawRect(p.sx, p.sy, hw, hh, 1.0f - hpColor, hpColor, 0.0f, 0.8f, sw, sh);
            }
            if (g_espData.drawTracelines) {
                DrawLine(p.sx, p.sy, sw * 0.5f, sh, 1.0f, 0.0f, 0.0f, 0.3f, sw, sh);
            }
            if (g_espData.drawLabels) {
                char label[64];
                snprintf(label, sizeof(label), "HP:%.0f %.0fm", p.hp, p.dist);
                DrawString(p.sx - hw, p.sy - hh - 12, label, 1.0f, 1.0f, 1.0f, 0.8f, sw, sh);
            }
        }
    }

    // ── Items ──
    if (g_espData.drawObjects) {
        for (int i = 0; i < g_espData.itemCount; i++) {
            auto& e = g_espData.items[i];
            float scale = 40.0f / (e.dist * 0.5f + 1.0f);

            // Color tier by credit value so high-value scrap pops:
            //  <20 junk=green, 20-59 common=yellow, 60-139 rare=orange, >=140 valuable=red.
            float r, g, b;
            if (e.value >= 140)      { r = 1.0f; g = 0.15f; b = 0.15f; } // valuable
            else if (e.value >= 60)  { r = 1.0f; g = 0.55f; b = 0.0f; } // rare
            else if (e.value >= 20)  { r = 1.0f; g = 0.85f; b = 0.1f; } // common
            else                     { r = 0.2f; g = 1.0f;  b = 0.3f; } // junk

            // Marker: a small box (heavier items get a bigger marker).
            float dotSize = (3.0f + (e.isHeavy ? 2.0f : 0.0f)) * scale;
            DrawRect(e.sx, e.sy, dotSize, dotSize, r, g, b, 0.85f, sw, sh);

            // Value bar: a horizontal bar whose length is proportional to credits
            // (capped at 200 for scaling), so you can gauge worth at a glance.
            float barMax = 18.0f * scale;
            float barLen = barMax * (e.value > 200 ? 1.0f : (float)e.value / 200.0f);
            DrawLine(e.sx - barMax * 0.5f, e.sy + dotSize + 3.0f * scale,
                     e.sx - barMax * 0.5f + barLen, e.sy + dotSize + 3.0f * scale,
                     r, g, b, 0.9f, sw, sh);

            if (g_espData.drawLabels) {
                char label[48];
                snprintf(label, sizeof(label), "$%d %.0fm", e.value, e.dist);
                DrawString(e.sx + dotSize + 4, e.sy - 6, label, r, g, b, 0.9f, sw, sh);
            }

            // Optional item name (from InventoryItem.itemName) under the marker.
            if (g_espData.showNames && e.name[0]) {
                DrawString(e.sx - dotSize, e.sy + dotSize + 6.0f * scale,
                           e.name, 1.0f, 1.0f, 1.0f, 0.85f, sw, sh);
            }
        }
    }

    // Restore ALL modified GL state
    glBindVertexArray(prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, prevVbo);
    glUseProgram(prevProg);
    if (prevBlendEnabled)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
    glBlendFunc(prevBlendSrc, prevBlendDst);
    if (prevDepthEnabled)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    if (prevScissorEnabled)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);
    if (prevCullEnabled)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    glActiveTexture((GLenum)prevActiveTexture);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTexture2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
    glBindSampler(prevActiveTexture - GL_TEXTURE0, (GLuint)prevSamplerBinding);

    g_espData.dataReady = false;
    pthread_mutex_unlock(&g_espData.mutex);
}