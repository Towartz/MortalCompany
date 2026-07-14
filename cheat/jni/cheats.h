#pragma once
#include "il2cpp.h"
#include "config.h"

// Resolve all game object field offsets from IL2CPP metadata.
// Called on first activation and re-resolved after scene transitions.
// Safe to call multiple times — returns immediately if already resolved.
void ResolveOffsets();

// Install Dobby hooks for all economy-related SyncVar interceptors.
// Called from both the main thread (ApplyCheats) and render thread
// (ApplyEconomyCheats) — the hook-setup flags ensure one-shot execution.
void SetupEconomyHooks();

// Full cheat application: resolve offsets, process all 5 slot-throttled
// cheat groups, per-frame economy writes, and scene-transition GC handling.
void ApplyCheats();

// Lightweight economy-only cheat loop: hook setup + field writes.
// Safe to call from render thread (eglSwapBuffers hook).
// No Mirror API calls — CmdAddMoney stays in ApplyCheats on the main thread.
void ApplyEconomyCheats();
