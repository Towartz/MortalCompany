#pragma once
#include "il2cpp.h"
#include "config.h"
#include "esp.h"

void ApplyCheats();
void ResolveOffsets();
void SetupEconomyHooks();
void AutoReward_GiveReward();
void AutoReward_SwitchToOnline();

// World-space position helpers (defined in esp.cpp)
Unity::Vector3 GetPos(void* transform);
void SetPos(void* transform, Unity::Vector3 p);
void* GetTransformOf(void* obj);

extern int g_adsShowedAd;
extern int g_adsOpenOnlineMode;
extern bool g_requestBypassReward;
extern bool g_requestSwitchOnline;
