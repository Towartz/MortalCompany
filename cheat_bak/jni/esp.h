#pragma once

#include <signal.h>
#include <setjmp.h>

// ══════════════════════════════════════════════════════════════════════
// CRASH GUARD — catches SIGSEGV/SIGBUS so a bad memory read doesn't kill the game
// ── Uses sigaction + siglongjmp to recover safely.
// ══════════════════════════════════════════════════════════════════════

extern thread_local sigjmp_buf g_crashJmpBuf;
extern thread_local volatile bool g_crashGuardActive;

void InstallCrashGuard();

#define CRASH_GUARD(code) do { \
    InstallCrashGuard(); \
    g_crashGuardActive = true; \
    if (sigsetjmp(g_crashJmpBuf, 1) == 0) { \
        code; \
    } else { \
        __android_log_print(ANDROID_LOG_ERROR, "FBK", "CRASH GUARD: recovered from crash at " #code); \
    } \
    g_crashGuardActive = false; \
} while(0)

// ── Main-thread ESP data gathering ──
// Does FindObjectsOfType, WorldToScreen, populates g_espData shared buffer.
// Runs on a dedicated background thread to avoid blocking the main thread.
void GatherESPMainThread();

// ── Render-thread ESP rendering (called from eglSwapBuffers hook) ──
// Reads g_espData shared buffer, draws with ImDrawList.
// Does NOT call any Unity APIs — safe from render thread.
void RenderESPRenderThread();

// ── Main-thread touch gathering (called from FP_Controller::Update hook) ──
// Polls Input.get_mousePosition, Input.get_touchCount etc.
// Stores in g_espData.touch for render thread to consume.
void GatherTouchMainThread();

// ── ESP background worker thread ──
// Runs GatherESPMainThread() on a dedicated thread so it never blocks
// the main thread's touch input pipeline.
void StartESPWorkerThread();
void TriggerESPRefresh();
void StopESPWorkerThread();
