#pragma once
#include "il2cpp.h"
#include "config.h"

void ApplyCheats();

// Lightweight economy cheats: hook setup + field writes only.
// Safe to call from render thread (no Mirror API calls — CmdAddMoney stays
// in ApplyCheats on the main thread). Use this to make economy cheats
// work in ALL scenes, not just ones where FP_Controller exists.
void ApplyEconomyCheats();
