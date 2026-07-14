#pragma once

#include <signal.h>
#include <setjmp.h>
#include <android/log.h>

extern thread_local sigjmp_buf g_crashJmpBuf;
extern thread_local volatile bool g_crashGuardActive;

void InstallCrashGuard();

#define CRASH_GUARD(code) do { \
    InstallCrashGuard(); \
    g_crashGuardActive = true; \
    if (sigsetjmp(g_crashJmpBuf, 1) == 0) { \
        code; \
    } else { \
        __android_log_print(ANDROID_LOG_ERROR, "FBK", "CRASH GUARD: recovered at " #code); \
    } \
    g_crashGuardActive = false; \
} while(0)
