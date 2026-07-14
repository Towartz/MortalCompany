#include "cheats.h"
#include "hook.h"
#include <android/log.h>
#include <cmath> // for NAN, std::isfinite

// ── Value-guarded write helpers ──
// Only write when the value actually differs. This avoids unnecessary memory bus
// traffic and cache line invalidations on ARM CPUs.
static inline void SetFloatIfChanged(void* obj, int offset, float target) {
    if (offset < 0) return;
    float cur = AsClass(obj)->GetMemberValue<float>(offset);
    if (cur != target)
        AsClass(obj)->SetMemberValue<float>(offset, target);
}
static inline void SetIntIfChanged(void* obj, int offset, int target) {
    if (offset < 0) return;
    int cur = AsClass(obj)->GetMemberValue<int>(offset);
    if (cur != target)
        AsClass(obj)->SetMemberValue<int>(offset, target);
}
static inline void SetU8IfChanged(void* obj, int offset, uint8_t target) {
    if (offset < 0) return;
    uint8_t cur = AsClass(obj)->GetMemberValue<uint8_t>(offset);
    if (cur != target)
        AsClass(obj)->SetMemberValue<uint8_t>(offset, target);
}
static inline void SetBoolIfChanged(void* obj, int offset, bool target) {
    if (offset < 0) return;
    bool cur = AsClass(obj)->GetMemberValue<bool>(offset);
    if (cur != target)
        AsClass(obj)->SetMemberValue<bool>(offset, target);
}
// Raw pointer write helper (no compare — pointers are volatile)
static inline void SetPtr(void* obj, int offset, void* target) {
    if (offset < 0) return;
    AsClass(obj)->SetMemberValue<void*>(offset, target);
}

// ── Offsets ────────────────────────────────────────────────────────
static int g_staminaWasteSpeed  = -1;
static int g_staminaGainSpeed   = -1;
static int g_staminaCanRun      = -1;
static int g_healthPoints       = -1;
static int g_healthIsDead       = -1;
static int g_healthUndamage     = -1;
static int g_flashWasteSpeed    = -1;
static int g_flashDischarge     = -1;
static int g_flashBatteryField  = -1;
static int g_sliderValue        = -1;
static int g_elecCapacity       = -1;
static int g_elecDischargeSpeed = -1;
static int g_elecIsDischarged   = -1;
static int g_adsShowedAd        = -1;
static int g_adsOpenOnlineMode  = -1;
static int g_fpWalkSpeed        = -1;
static int g_fpRunSpeed         = -1;
static int g_fpJumpForce        = -1;
static int g_fpGravity          = -1;
static int g_fpAirControl       = -1;
static int g_fpFallDamageVel    = -1;
static int g_invMaxWeight       = -1;
static int g_magnetStrength     = -1;
static int g_magnetMaxItems     = -1;
static int g_doorSeconds        = -1;
static int g_doorDelayBuffer    = -1;
static int g_doorStopDelayCor   = -1;
static int g_enemyLocalPlayer   = -1;
static int g_enemyNavMesh       = -1;
static int g_enemyActivePlayers = -1;
static int g_fpGrounded         = -1;
static int g_fpCanJump          = -1;

// Economy offsets
static int g_shopMoney          = -1;
static int g_shopTotalPrice     = -1;
static int g_quotaDeadline      = -1;
static int g_quotaAmount        = -1;
static int g_quotaTotalCollected = -1;
static int g_itemValue          = -1;
static int g_mineExploded       = -1;
static int g_shipDoorPower      = -1;
static int g_teleTimeout        = -1;
static int g_teleTimePassed     = -1;

static bool g_resolved = false;
static bool g_inTransition = false; // set true when camera goes null during scene transition
static int g_transitionCooldown = 0; // frames to wait after transition end for GC cleanup
static IL2CPPCache::SceneType g_sceneType = IL2CPPCache::SceneType::Unknown; // current scene classification

// ── SyncVar hook forwarding declarations ───────────────────────────
// (Implementations follow after ResolveOffsets)
static bool g_moneyHookSetup = false;
typedef void (*MoneyChanged_t)(void*, int, int, void*);
static MoneyChanged_t orig_onMoneyAmountChanged = nullptr;

static bool g_totalPriceHookSetup = false;
typedef void (*CalcPrice_t)(void*, void*);
static CalcPrice_t orig_CalculateTotalPrice = nullptr;

static bool g_elecCapHookSetup = false;
typedef void (*SetNetFloat_t)(void*, float, void*);
static SetNetFloat_t orig_set_Networkcapacity = nullptr;

static bool g_elecDiscHookSetup = false;
typedef void (*SetNetBool_t)(void*, bool, void*);
static SetNetBool_t orig_set_NetworkisDiscarged = nullptr;

static bool g_teleHookSetup = false;
typedef void (*SetNetTimePassed_t)(void*, int, void*);
static SetNetTimePassed_t orig_set_NetworktimePassed = nullptr;

static bool g_scrapHookSetup = false;
typedef void (*SetNetIsActive_t)(void*, bool, void*);
static SetNetIsActive_t orig_set_NetworkisActive = nullptr;

// ── Auto Reward hook: intercepts RewardedAdsForOnline.OnOnlineModeButtonPressed
//    and bypasses the ad SDK by calling onAdComleted directly.
static bool g_autoRewardHookSetup = false;
typedef void (*OnOnlineModePressed_t)(void*);
static OnOnlineModePressed_t orig_OnOnlineModePressed = nullptr;

static void my_OnOnlineModePressed(void* instance) {
    if (g_config.autoReward) {
        LOGD("autoReward: intercepted OnOnlineModeButtonPressed — bypassing ad");
        if (g_adsShowedAd >= 0)
            AsClass(instance)->SetMemberValue<bool>(g_adsShowedAd, true);

        InvokeManaged(instance, "RewardedAdsForOnline", "onAdComleted", 0, nullptr);

        if (g_adsOpenOnlineMode >= 0) {
            void* unityEvent = AsClass(instance)->GetMemberValue<void*>(g_adsOpenOnlineMode);
            if (unityEvent)
                InvokeManaged(unityEvent, "UnityEngine.Events.UnityEvent", "Invoke", 0, nullptr);
        }

        g_config.autoReward = false;
        return;
    }
    if (orig_OnOnlineModePressed)
        orig_OnOnlineModePressed(instance);
}

static bool g_quotaHookSetup = false;
typedef void (*UpdateView_t)(void*, int, int, void*);
static UpdateView_t orig_quota_updateView = nullptr;

// ── Restore State ──
static struct {
    // Previous toggle states (for ON→OFF / OFF→ON detection)
    bool prevInfiniteStamina      = false;
    bool prevUnlimitedSprint      = false;
    bool prevSpeedHack            = false;
    bool prevSuperJump            = false;
    bool prevInfinityJumps        = false;
    bool prevNoGravity            = false;
    bool prevAirControl           = false;
    bool prevNoFallDamage         = false;
    bool prevGodMode              = false;
    bool prevStaminaManipulation  = false;
    bool prevUnlimitedFlashlight  = false;
    bool prevUnlimitedUsage       = false;
    bool prevInfiniteJetpackFuel  = false;
    bool prevNoWeightLimit        = false;
    bool prevSuperItemMagnet      = false;
    bool prevAutoPickup           = false;
    bool prevSetScrapValue        = false;
    bool prevUnlimitedMoney       = false;
    bool prevFreeItems            = false;
    bool prevQuotaManipulation    = false;
    bool prevInstantDoors         = false;
    bool prevDisableMines         = false;
    bool prevShipDoorAlwaysOpen   = false;
    bool prevNoTeleporterCooldown = false;

    // Saved original field values (initialized to NaN / INT_MIN so we can detect uninitialized)
    float origStaminaWasteSpeed = NAN;
    float origStaminaGainSpeed  = NAN;
    uint8_t origCanRun          = 0;
    float origWalkSpeed         = NAN;
    float origRunSpeed          = NAN;
    float origJumpForce         = NAN;
    bool  origCanJump           = true;
    float origGravity           = NAN;
    bool  origAirControl        = false;
    float origFallDamageVel     = NAN;
    float origHealthPoints      = NAN;
    uint8_t origIsDead          = 0;
    bool  origUndamageable      = false;
    float origFlashWasteSpeed   = NAN;
    uint8_t origFlashDischarge  = 0;
    float origSliderValue       = NAN;
    float origElecCapacity      = NAN;
    float origElecDischargeSpeed = NAN;
    uint8_t origElecIsDischarged = 0;
    float origInvMaxWeight      = NAN;
    float origMagnetStrength    = NAN;
    int   origMagnetMaxItems    = INT_MIN;
    int   origItemValue         = INT_MIN;
    int   origShopMoney         = INT_MIN;
    int   origShopTotalPrice    = INT_MIN;
    int   origQuotaDeadline     = INT_MIN;
    int   origQuotaAmount       = INT_MIN;
    int   origQuotaTotalCollected = INT_MIN;
    float origDoorSeconds       = NAN;
    uint8_t origMineExploded    = 0;
    int   origShipDoorPower     = INT_MIN;
} g_r;

// ════════════════════════════════════════════════════════════════════
// RESOLVE OFFSETS
// ════════════════════════════════════════════════════════════════════

void ResolveOffsets() {
    if (g_resolved) return;

    using namespace IL2CPPCache;
    bool isMenuScene = (g_sceneType == SceneType::MainMenu);
    bool isGameScene = (g_sceneType == SceneType::Ship ||
                        g_sceneType == SceneType::Moon ||
                        g_sceneType == SceneType::Facility ||
                        g_sceneType == SceneType::Other ||
                        g_sceneType == SceneType::Unknown);

    LOGD("ResolveOffsets: scene=%s isGame=%d",
         SceneTypeName(g_sceneType), (int)isGameScene);

    // ── Always-present: UI / Battery slider (loaded in every scene) ───────
    // These use RESOLVE_FIELD_ONCE — once found they persist across scenes,
    // since the Slider class is from UnityEngine and never unloads.
    RESOLVE_FIELD_ONCE(g_sliderValue, "Slider", "m_Value");

    // ── Gameplay-only classes — skip in main menu to avoid logspam ────────
    // When g_sceneType is MainMenu these classes don't exist yet.
    // Because failures are NOT cached, the next scene will retry them.
    if (isGameScene) {

        // ── Stamina ───────────────────────────────────────────────────────
        RESOLVE_FIELD(g_staminaWasteSpeed,  "Stamina",       "wasteSpeed");
        RESOLVE_FIELD(g_staminaGainSpeed,   "Stamina",       "gainSpeed");
        RESOLVE_FIELD(g_staminaCanRun,      "Stamina",       "canRun");

        // ── Health ────────────────────────────────────────────────────────
        RESOLVE_FIELD(g_healthPoints,       "Health",        "healthPoints");
        RESOLVE_FIELD(g_healthIsDead,       "Health",        "isDead");
        RESOLVE_FIELD(g_healthUndamage,     "Health",        "UndamageAble");

        // ── Flashlight / Battery ──────────────────────────────────────────
        RESOLVE_FIELD(g_flashWasteSpeed,    "BatterySlider", "wasteSpeed");
        RESOLVE_FIELD(g_flashDischarge,     "BatterySlider", "discharge");
        RESOLVE_FIELD(g_flashBatteryField,  "BatterySlider", "Battery");

        // ── Electric items ────────────────────────────────────────────────
        RESOLVE_FIELD(g_elecCapacity,       "ElectricItem",  "capacity");
        RESOLVE_FIELD(g_elecDischargeSpeed, "ElectricItem",  "dischargeSpeed");
        RESOLVE_FIELD(g_elecIsDischarged,   "ElectricItem",  "isDiscarged");

        // ── Ads / Reward (ship scene UI) ──────────────────────────────────
        RESOLVE_FIELD(g_adsShowedAd,        "RewardedAdsForOnline", "showedAd");
        RESOLVE_FIELD(g_adsOpenOnlineMode,  "RewardedAdsForOnline", "openOnlineMode");

        // ── FP_Controller — only active in gameplay scenes ────────────────
        RESOLVE_FIELD(g_fpWalkSpeed,        "FP_Controller", "walkSpeed");
        RESOLVE_FIELD(g_fpRunSpeed,         "FP_Controller", "runSpeed");
        RESOLVE_FIELD(g_fpJumpForce,        "FP_Controller", "jumpForce");
        RESOLVE_FIELD(g_fpGravity,          "FP_Controller", "gravity");
        RESOLVE_FIELD(g_fpAirControl,       "FP_Controller", "airControl");
        RESOLVE_FIELD(g_fpFallDamageVel,    "FP_Controller", "velocityToTakeDamage");
        RESOLVE_FIELD(g_fpGrounded,         "FP_Controller", "grounded");
        RESOLVE_FIELD(g_fpCanJump,          "FP_Controller", "canJump");

        // ── Inventory / Magnet ────────────────────────────────────────────
        RESOLVE_FIELD(g_invMaxWeight,       "Inventory",     "maxWeightToCarry");
        RESOLVE_FIELD(g_magnetStrength,     "ItemMagnet",    "attractionStrength");
        RESOLVE_FIELD(g_magnetMaxItems,     "ItemMagnet",    "maxItemsAmountToAttract");

        // ── Doors ─────────────────────────────────────────────────────────
        RESOLVE_FIELD(g_doorSeconds,        "OpenDoorTrigger", "secondsToOpenDoor");
        RESOLVE_FIELD(g_doorDelayBuffer,    "OpenDoorTrigger", "delayBuffer");
        RESOLVE_FIELD(g_doorStopDelayCor,   "OpenDoorTrigger", "stopDelayCoroutine");

        // ── Enemies (facility/moon only, but metadata is always loaded) ───
        RESOLVE_FIELD(g_enemyLocalPlayer,   "EnemyBase",     "LocalPlayer");
        RESOLVE_FIELD(g_enemyNavMesh,       "EnemyBase",     "navMeshAgent");
        RESOLVE_FIELD(g_enemyActivePlayers, "EnemyBase",     "ActivePlayers");

        // ── Economy (ship scene) ──────────────────────────────────────────
        RESOLVE_FIELD(g_shopMoney,          "TerminalShop",  "Money");
        RESOLVE_FIELD(g_shopTotalPrice,     "TerminalShop",  "TotalPrice");
        RESOLVE_FIELD(g_quotaDeadline,      "quota",         "deadline");
        RESOLVE_FIELD(g_quotaAmount,        "quota",         "quotaAmount");
        RESOLVE_FIELD(g_quotaTotalCollected,"quota",         "totalCollected");
        RESOLVE_FIELD(g_itemValue,          "InventoryItem", "Value");

        // ── Traps / Teleporter ────────────────────────────────────────────
        RESOLVE_FIELD(g_mineExploded,       "MineTrigger",        "alreadyExploded");
        RESOLVE_FIELD(g_shipDoorPower,      "ShipDoorController", "doorPower");
        RESOLVE_FIELD(g_teleTimeout,        "TeleporterButton",   "timeout");
        RESOLVE_FIELD(g_teleTimePassed,     "TeleporterButton",   "timePassed");

    } // end isGameScene

    // ── Resolution summary ────────────────────────────────────────────────
    int ok = 0, fail = 0;
    int all[] = {
        g_staminaWasteSpeed, g_staminaGainSpeed, g_staminaCanRun,
        g_healthPoints, g_healthIsDead, g_healthUndamage,
        g_flashWasteSpeed, g_flashDischarge, g_flashBatteryField, g_sliderValue,
        g_elecCapacity, g_elecDischargeSpeed, g_elecIsDischarged,
        g_adsShowedAd, g_adsOpenOnlineMode,
        g_fpWalkSpeed, g_fpRunSpeed, g_fpJumpForce, g_fpGravity,
        g_fpAirControl, g_fpFallDamageVel, g_fpGrounded, g_fpCanJump,
        g_invMaxWeight, g_magnetStrength, g_magnetMaxItems,
        g_doorSeconds, g_doorDelayBuffer, g_doorStopDelayCor,
        g_enemyLocalPlayer, g_enemyNavMesh, g_enemyActivePlayers,
        g_shopMoney, g_shopTotalPrice, g_quotaDeadline, g_quotaAmount,
        g_quotaTotalCollected, g_itemValue,
        g_mineExploded, g_shipDoorPower, g_teleTimeout, g_teleTimePassed
    };
    for (int v : all) { if (v >= 0) ++ok; else ++fail; }
    LOGD("ResolveOffsets[%s]: %d ok, %d pending (will retry on next scene)",
         IL2CPPCache::SceneTypeName(g_sceneType), ok, fail);

    // Always mark resolved — pending fields will retry after next scene ClearAll()
    g_resolved = true;
}

// ════════════════════════════════════════════════════════════════════
// SYNCVAR HOOK IMPLEMENTATIONS
// ════════════════════════════════════════════════════════════════════

// ── onMoneyAmountChanged hook — blocks server SyncVar overrides ──
// TerminalShop.Money is [SyncVar(hook = "onMoneyAmountChanged")].
// The server syncs Money values to clients via Mirror. This hook intercepts
// the SyncVar callback so the server cannot lower money below our target.
static void my_onMoneyAmountChanged(void* shop, int oldValue, int newValue, void* method) {
    if (g_config.unlimitedMoney && g_shopMoney >= 0) {
        // Block server SyncVar override: keep money at target.
        // Only intercept when server tries to LOWER money below threshold.
        if (newValue < 90000) {
            // Write the field directly (belt + suspenders)
            AsClass(shop)->SetMemberValue<int>(g_shopMoney, 99999);
            // Forward to original with our target value so the UI updates properly
            if (orig_onMoneyAmountChanged)
                orig_onMoneyAmountChanged(shop, oldValue, 99999, method);
            return;
        }
    }
    // Pass through: call original hook with actual values
    if (orig_onMoneyAmountChanged)
        orig_onMoneyAmountChanged(shop, oldValue, newValue, method);
}

// ── CalculateTotalPrice hook — forces TotalPrice=0 after every recalculation ──
static void my_CalculateTotalPrice(void* shop, void* method) {
    // Call original first — let the game calculate normally
    if (orig_CalculateTotalPrice)
        orig_CalculateTotalPrice(shop, method);
    // Then force TotalPrice to 0 if free items is ON
    if (g_config.freeItems && g_shopTotalPrice >= 0)
        AsClass(shop)->SetMemberValue<int>(g_shopTotalPrice, 0);
}

// ── ElectricItem SyncVar hooks — blocks server overrides on capacity + isDiscarged ──
static void my_set_Networkcapacity(void* ei, float serverValue, void* method) {
    bool elecActive = g_config.unlimitedUsage || g_config.infiniteJetpackFuel;
    if (elecActive && g_elecCapacity >= 0 && serverValue < 999999.0f) {
        // Server is syncing capacity below our target — override
        if (orig_set_Networkcapacity)
            orig_set_Networkcapacity(ei, 999999.0f, method);
        return;
    }
    if (orig_set_Networkcapacity)
        orig_set_Networkcapacity(ei, serverValue, method);
}

static void my_set_NetworkisDiscarged(void* ei, bool serverValue, void* method) {
    bool elecActive = g_config.unlimitedUsage || g_config.infiniteJetpackFuel;
    if (elecActive && g_elecIsDischarged >= 0 && serverValue) {
        // Server is syncing isDiscarged=true — override to false
        if (orig_set_NetworkisDiscarged)
            orig_set_NetworkisDiscarged(ei, false, method);
        return;
    }
    if (orig_set_NetworkisDiscarged)
        orig_set_NetworkisDiscarged(ei, serverValue, method);
}

// ── TeleporterButton::set_NetworktimePassed hook — blocks server cooldown sync ──
static void my_set_NetworktimePassed(void* tp, int serverValue, void* method) {
    if (g_config.noTeleporterCooldown && g_teleTimeout >= 0 && g_teleTimePassed >= 0) {
        // Read the teleporter's timeout value and force timePassed to match it
        int timeout = AsClass(tp)->GetMemberValue<int>(g_teleTimeout);
        if (timeout > 0 && serverValue < timeout) {
            // Server is syncing a timePassed below timeout — override it
            if (orig_set_NetworktimePassed)
                orig_set_NetworktimePassed(tp, timeout, method);
            return;
        }
    }
    // Pass through: call original with server value
    if (orig_set_NetworktimePassed)
        orig_set_NetworktimePassed(tp, serverValue, method);
}

// ── InventoryItem::set_NetworkisActive hook — overrides scrap Value ──
static void my_set_NetworkisActive(void* item, bool value, void* method) {
    // Call original first — let Mirror update the isActive field normally
    if (orig_set_NetworkisActive)
        orig_set_NetworkisActive(item, value, method);
    // Then force Value to 999 if Set Scrap Value is ON
    if (g_config.setScrapValue && g_itemValue >= 0)
        AsClass(item)->SetMemberValue<int>(g_itemValue, 999);
}

// ── quota::updateView hook — blocks server SyncVar overrides on quota fields ──
static void my_quota_updateView(void* q, int oldValue, int newValue, void* method) {
    if (g_config.quotaManipulation && g_quotaDeadline >= 0 && g_quotaTotalCollected >= 0 && g_quotaAmount >= 0) {
        // Force our values BEFORE calling updateView so the UI reflects them
        AsClass(q)->SetMemberValue<int>(g_quotaDeadline, 99);
        int quotaAmt = AsClass(q)->GetMemberValue<int>(g_quotaAmount);
        AsClass(q)->SetMemberValue<int>(g_quotaTotalCollected, quotaAmt + 1000);
    }
    // Forward to original — it reads the (now-modified) fields and updates the UI
    if (orig_quota_updateView)
        orig_quota_updateView(q, oldValue, newValue, method);
}

// ════════════════════════════════════════════════════════════════════
// SETUP ECONOMY HOOKS (one-shot, guarded by static flags)
// ════════════════════════════════════════════════════════════════════

void SetupEconomyHooks() {
    // ── TerminalShop hooks ──
    if (!g_moneyHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("TerminalShop", "onMoneyAmountChanged", 2);
        if (ptr) {
            hook_func(ptr, (void*)my_onMoneyAmountChanged, (void**)&orig_onMoneyAmountChanged);
            g_moneyHookSetup = true;
            LOGD("Econ: onMoneyAmountChanged hooked");
        }
    }
    if (!g_totalPriceHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("TerminalShop", "CalculateTotalPrice", 0);
        if (ptr) {
            hook_func(ptr, (void*)my_CalculateTotalPrice, (void**)&orig_CalculateTotalPrice);
            g_totalPriceHookSetup = true;
            LOGD("Econ: CalculateTotalPrice hooked");
        }
    }
    // ── ElectricItem hooks ──
    if (!g_elecCapHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("ElectricItem", "set_Networkcapacity", 1);
        if (ptr) {
            hook_func(ptr, (void*)my_set_Networkcapacity, (void**)&orig_set_Networkcapacity);
            g_elecCapHookSetup = true;
            LOGD("Econ: set_Networkcapacity hooked");
        }
    }
    if (!g_elecDiscHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("ElectricItem", "set_NetworkisDiscarged", 1);
        if (ptr) {
            hook_func(ptr, (void*)my_set_NetworkisDiscarged, (void**)&orig_set_NetworkisDiscarged);
            g_elecDiscHookSetup = true;
            LOGD("Econ: set_NetworkisDiscarged hooked");
        }
    }
    // ── Teleporter hook ──
    if (!g_teleHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("TeleporterButton", "set_NetworktimePassed", 1);
        if (ptr) {
            hook_func(ptr, (void*)my_set_NetworktimePassed, (void**)&orig_set_NetworktimePassed);
            g_teleHookSetup = true;
            LOGD("Econ: set_NetworktimePassed hooked");
        }
    }
    // ── Scrap hook ──
    if (!g_scrapHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("InventoryItem", "set_NetworkisActive", 1);
        if (ptr) {
            hook_func(ptr, (void*)my_set_NetworkisActive, (void**)&orig_set_NetworkisActive);
            g_scrapHookSetup = true;
            LOGD("Econ: set_NetworkisActive hooked");
        }
    }
    // ── Quota hook ──
    if (!g_quotaHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("quota", "updateView", 2);
        if (ptr) {
            hook_func(ptr, (void*)my_quota_updateView, (void**)&orig_quota_updateView);
            g_quotaHookSetup = true;
            LOGD("Econ: quota::updateView hooked");
        }
    }

    // ── Auto Reward hook (installed once, intercepts OnOnlineModeButtonPressed) ──
    if (!g_autoRewardHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("RewardedAdsForOnline", "OnOnlineModeButtonPressed", 0);
        if (ptr) {
            hook_func(ptr, (void*)my_OnOnlineModePressed, (void**)&orig_OnOnlineModePressed);
            g_autoRewardHookSetup = true;
            LOGD("autoReward: OnOnlineModeButtonPressed hooked");
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// APPLY CHEATS (main thread — called from FP_Controller::Update hook)
// ════════════════════════════════════════════════════════════════════

void ApplyCheats() {
    if (!IL2CPP::Globals.m_GameAssembly) return;

    // ── Resolve offsets on first call (and after scene transitions) ──
    ResolveOffsets();

    // ── Setup economy hooks eagerly from the main thread ──
    // (ApplyEconomyCheats also does this from the render thread; the
    //  static g_*HookSetup flags ensure each hook is installed exactly once.)
    SetupEconomyHooks();

    // ── Top-level fast path: skip everything when nothing is active ──
    // Checking all hack states + prev-states so we don't skip mid-restore.
    if (!g_config.infiniteStamina && !g_config.unlimitedSprint && !g_config.speedHack &&
        !g_config.superJump && !g_config.infinityJumps && !g_config.noGravity && !g_config.airControl && !g_config.noFallDamage &&
        !g_config.godMode && !g_config.staminaManipulation &&
        !g_config.unlimitedFlashlight && !g_config.unlimitedUsage && !g_config.infiniteJetpackFuel &&
        !g_config.noWeightLimit && !g_config.superItemMagnet && !g_config.autoPickup &&
        !g_config.setScrapValue && !g_config.autoReward &&
        !g_config.oneHitKill && !g_config.instantKillAll && !g_config.blindEnemies &&
        !g_config.unlimitedMoney && !g_config.freeItems && !g_config.quotaManipulation &&
        !g_config.instantDoors && !g_config.disableMines && !g_config.shipDoorAlwaysOpen &&
        !g_config.noTeleporterCooldown &&
        !g_config.espPlayers && !g_config.espObjects &&
        !g_r.prevInfiniteStamina && !g_r.prevUnlimitedSprint && !g_r.prevSpeedHack &&
        !g_r.prevSuperJump && !g_r.prevInfinityJumps && !g_r.prevNoGravity && !g_r.prevAirControl && !g_r.prevNoFallDamage &&
        !g_r.prevGodMode && !g_r.prevStaminaManipulation &&
        !g_r.prevUnlimitedFlashlight && !g_r.prevUnlimitedUsage && !g_r.prevInfiniteJetpackFuel &&
        !g_r.prevNoWeightLimit && !g_r.prevSuperItemMagnet && !g_r.prevAutoPickup &&
        !g_r.prevSetScrapValue &&
        !g_r.prevUnlimitedMoney && !g_r.prevFreeItems && !g_r.prevQuotaManipulation &&
        !g_r.prevInstantDoors && !g_r.prevDisableMines && !g_r.prevShipDoorAlwaysOpen &&
        !g_r.prevNoTeleporterCooldown)
    {
        return;
    }

    // ── Slot cycle statics ──
    // Declared early so scene-change detection below can reset them.
    static int g_slot = 0;
    static int g_findThrottle = 0;

    // ── Scene-change detection + GC cleanup guard ──
    // Runs BEFORE the transition guard so we always detect end-of-transition
    // (guard would otherwise skip the detection code, never clearing the flag).
    // After transition end, a cooldown period lets Unity's GC finish collecting
    // destroyed objects before we resume FindObjectsOfType calls.
    {
        static bool g_prevCamHadTransform = false;
        bool camHasTransform = false;
        Unity::CCamera* cam = Unity::Camera::GetMain();
        if (cam && reinterpret_cast<Unity::CComponent*>(cam)->GetTransform())
            camHasTransform = true;

        // Transition START: camera was valid, now null → scene unloading
        if (g_prevCamHadTransform && !camHasTransform) {
            LOGD("Cheats: transition START — pausing for GC cleanup");
            g_inTransition = true;
            g_transitionCooldown = 0;
            // ── Wipe resolver caches NOW ──────────────────────────────────
            // This is the critical fix for scene-dependent classes:
            // failures were previously cached as -1 and would never retry.
            // Clearing here means the next ResolveOffsets() call gets a
            // completely fresh lookup for every class in every scene.
            IL2CPPCache::ClearAll();
        }

        // Transition END: camera was null, now valid → new scene loaded
        // Don't clear g_inTransition yet — wait for GC cleanup cooldown
        if (!g_prevCamHadTransform && camHasTransform) {
            // ── Detect new scene type ─────────────────────────────────────
            int sceneIdx = IL2CPPCache::GetActiveSceneIndex();
            g_sceneType  = IL2CPPCache::ClassifyScene(sceneIdx);
            LOGD("Cheats: transition END — scene index=%d type=%s — waiting 5 frames for GC",
                 sceneIdx, IL2CPPCache::SceneTypeName(g_sceneType));
            g_transitionCooldown = 5;
            g_resolved = false;   // will re-run ResolveOffsets() for this scene
            g_r = {};
            g_slot = -1;
            g_findThrottle = 2;
        }
        g_prevCamHadTransform = camHasTransform;
    }

    // ── GC cleanup cooldown ──
    // After a transition ends, Unity's GC may still be collecting destroyed objects
    // from the old scene. Wait N frames before resuming to avoid accessing freed memory.
    if (g_transitionCooldown > 0) {
        g_transitionCooldown--;
        if (g_transitionCooldown == 0) {
            g_inTransition = false;
            LOGD("Cheats: GC cleanup complete — resuming processing");
        }
    }

    // ── Transition guard: skip processing during scene loads + GC cleanup ──
    if (g_inTransition) return;

    // ── Slot cycle: spread hack processing across 5 frames ──
    g_slot = (g_slot + 1) % 5;

    // ════════════════════════════════════════════════════════════════
    // MOVEMENT
    // ════════════════════════════════════════════════════════════════

    // ── Slot 0: Stamina (Infinite Stamina + Unlimited Sprint) ─────
    if (g_slot == 0)
    {
        bool anyStamina = g_config.infiniteStamina || g_config.unlimitedSprint || g_config.staminaManipulation;
        bool prevAnyStamina = g_r.prevInfiniteStamina || g_r.prevUnlimitedSprint || g_r.prevStaminaManipulation;

        if (anyStamina || prevAnyStamina) {
            auto* arr = Unity::Object::FindObjectsOfType<void>("Stamina");
            void* st = (arr && arr->m_uMaxLength > 0) ? arr->At(0) : nullptr;

            // ON→OFF: restore stamina fields
            if (!anyStamina && prevAnyStamina && st) {
                if (g_staminaWasteSpeed >= 0 && std::isfinite(g_r.origStaminaWasteSpeed))
                    SetFloatIfChanged(st, g_staminaWasteSpeed, g_r.origStaminaWasteSpeed);
                if (g_staminaGainSpeed >= 0 && std::isfinite(g_r.origStaminaGainSpeed))
                    SetFloatIfChanged(st, g_staminaGainSpeed, g_r.origStaminaGainSpeed);
                if (g_staminaCanRun >= 0 && g_r.prevUnlimitedSprint)
                    SetU8IfChanged(st, g_staminaCanRun, g_r.origCanRun);
            }
            // OFF→ON: apply hacks
            else if (anyStamina && st) {
                bool justEnabled = !prevAnyStamina;
                if (justEnabled) {
                    if (g_staminaWasteSpeed >= 0) g_r.origStaminaWasteSpeed = AsClass(st)->GetMemberValue<float>(g_staminaWasteSpeed);
                    if (g_staminaGainSpeed  >= 0) g_r.origStaminaGainSpeed  = AsClass(st)->GetMemberValue<float>(g_staminaGainSpeed);
                    if (g_staminaCanRun     >= 0) g_r.origCanRun = AsClass(st)->GetMemberValue<uint8_t>(g_staminaCanRun);
                }

                if (g_config.infiniteStamina || g_config.staminaManipulation) {
                    // Subtle: set waste to near-zero instead of absolute 0
                    if (g_staminaWasteSpeed >= 0) SetFloatIfChanged(st, g_staminaWasteSpeed, 0.001f);
                    if (g_staminaGainSpeed  >= 0) SetFloatIfChanged(st, g_staminaGainSpeed, 50.0f);
                }
                if (g_config.unlimitedSprint && g_staminaCanRun >= 0)
                    SetU8IfChanged(st, g_staminaCanRun, 1);
            }
        }

        g_r.prevInfiniteStamina     = g_config.infiniteStamina;
        g_r.prevUnlimitedSprint     = g_config.unlimitedSprint;
        g_r.prevStaminaManipulation = g_config.staminaManipulation;
    }

    // ── Slot 1: Movement: speed, jump, gravity, air, fall ──────────
    if (g_slot == 1)
    {
        bool anyMove   = g_config.speedHack || g_config.superJump || g_config.noGravity || g_config.airControl || g_config.noFallDamage;
        bool prevAnyMove = g_r.prevSpeedHack || g_r.prevSuperJump || g_r.prevNoGravity || g_r.prevAirControl || g_r.prevNoFallDamage;

        if (anyMove || prevAnyMove) {
            auto* fpArr = Unity::Object::FindObjectsOfType<void>("FP_Controller");
            void* fp = (fpArr && fpArr->m_uMaxLength > 0) ? fpArr->At(0) : nullptr;

            // ON→OFF: restore all movement fields
            if (!anyMove && prevAnyMove && fp) {
                if (g_fpWalkSpeed     >= 0 && std::isfinite(g_r.origWalkSpeed))    SetFloatIfChanged(fp, g_fpWalkSpeed, g_r.origWalkSpeed);
                if (g_fpRunSpeed      >= 0 && std::isfinite(g_r.origRunSpeed))     SetFloatIfChanged(fp, g_fpRunSpeed, g_r.origRunSpeed);
                if (g_fpJumpForce     >= 0 && std::isfinite(g_r.origJumpForce))    SetFloatIfChanged(fp, g_fpJumpForce, g_r.origJumpForce);
                if (g_fpGravity       >= 0 && std::isfinite(g_r.origGravity))      SetFloatIfChanged(fp, g_fpGravity, g_r.origGravity);
                if (g_fpAirControl    >= 0)                                          SetBoolIfChanged(fp, g_fpAirControl, g_r.origAirControl);
                if (g_fpFallDamageVel >= 0 && std::isfinite(g_r.origFallDamageVel)) SetFloatIfChanged(fp, g_fpFallDamageVel, g_r.origFallDamageVel);
            }
            // OFF→ON: save originals before modifying
            else if (!prevAnyMove && anyMove && fp) {
                if (g_fpWalkSpeed     >= 0) g_r.origWalkSpeed     = AsClass(fp)->GetMemberValue<float>(g_fpWalkSpeed);
                if (g_fpRunSpeed      >= 0) g_r.origRunSpeed      = AsClass(fp)->GetMemberValue<float>(g_fpRunSpeed);
                if (g_fpJumpForce     >= 0) g_r.origJumpForce     = AsClass(fp)->GetMemberValue<float>(g_fpJumpForce);
                if (g_fpGravity       >= 0) g_r.origGravity       = AsClass(fp)->GetMemberValue<float>(g_fpGravity);
                if (g_fpAirControl    >= 0) g_r.origAirControl    = AsClass(fp)->GetMemberValue<bool>(g_fpAirControl);
                if (g_fpFallDamageVel >= 0) g_r.origFallDamageVel = AsClass(fp)->GetMemberValue<float>(g_fpFallDamageVel);
            }

            if (fp) {
                // ── Subtle speed boost (less obvious than 15/25) ──
                if (g_config.speedHack) {
                    if (g_fpWalkSpeed >= 0) SetFloatIfChanged(fp, g_fpWalkSpeed, 8.0f);
                    if (g_fpRunSpeed  >= 0) SetFloatIfChanged(fp, g_fpRunSpeed,  12.0f);
                }
                // ── Subtle jump boost ──
                if (g_config.superJump && g_fpJumpForce >= 0)
                    SetFloatIfChanged(fp, g_fpJumpForce, 8.0f);
                if (g_config.noGravity && g_fpGravity >= 0)
                    SetFloatIfChanged(fp, g_fpGravity, 0.0f);
                if (g_config.airControl && g_fpAirControl >= 0)
                    SetBoolIfChanged(fp, g_fpAirControl, true);
                if (g_config.noFallDamage && g_fpFallDamageVel >= 0)
                    SetFloatIfChanged(fp, g_fpFallDamageVel, 99999.0f);
            }
        }

        g_r.prevSpeedHack    = g_config.speedHack;
        g_r.prevSuperJump    = g_config.superJump;
        g_r.prevNoGravity    = g_config.noGravity;
        g_r.prevAirControl   = g_config.airControl;
        g_r.prevNoFallDamage = g_config.noFallDamage;
    }

    // ════════════════════════════════════════════════════════════════
    // PLAYER
    // ════════════════════════════════════════════════════════════════

    // ── Slot 2: God Mode ──────────────────────────────────────────
    if (g_slot == 2)
    {
        if (g_config.godMode || g_r.prevGodMode) {
            auto* arr = Unity::Object::FindObjectsOfType<void>("PlayerHealth");
            if (!arr || arr->m_uMaxLength == 0)
                arr = Unity::Object::FindObjectsOfType<void>("Health");
            void* ph = (arr && arr->m_uMaxLength > 0) ? arr->At(0) : nullptr;

            // ON→OFF: restore health fields
            if (!g_config.godMode && g_r.prevGodMode && ph) {
                if (g_healthPoints   >= 0 && std::isfinite(g_r.origHealthPoints)) SetFloatIfChanged(ph, g_healthPoints, g_r.origHealthPoints);
                if (g_healthIsDead   >= 0)                                         SetU8IfChanged(ph, g_healthIsDead, g_r.origIsDead);
                if (g_healthUndamage >= 0)                                         SetBoolIfChanged(ph, g_healthUndamage, g_r.origUndamageable);
            }
            // OFF→ON: apply god mode
            else if (g_config.godMode && ph) {
                if (!g_r.prevGodMode) {
                    if (g_healthPoints   >= 0) g_r.origHealthPoints  = AsClass(ph)->GetMemberValue<float>(g_healthPoints);
                    if (g_healthIsDead   >= 0) g_r.origIsDead        = AsClass(ph)->GetMemberValue<uint8_t>(g_healthIsDead);
                    if (g_healthUndamage >= 0) g_r.origUndamageable  = AsClass(ph)->GetMemberValue<bool>(g_healthUndamage);
                }
                // Keep health at 200 instead of 500 — more subtle, still very tanky
                if (g_healthPoints   >= 0) SetFloatIfChanged(ph, g_healthPoints, 200.0f);
                if (g_healthIsDead   >= 0) SetU8IfChanged(ph, g_healthIsDead, 0);
                if (g_healthUndamage >= 0) SetBoolIfChanged(ph, g_healthUndamage, true);
            }
        }

        g_r.prevGodMode = g_config.godMode;
    }

    // ════════════════════════════════════════════════════════════════
    // ITEMS (Slot 3)
    // ════════════════════════════════════════════════════════════════

    // ── Slot 3: Items (Flashlight, Electric, Weight, Magnet, Scrap, Reward) ──
    if (g_slot == 3)
    {
        bool anyActive = g_config.unlimitedFlashlight || g_config.unlimitedUsage ||
            g_config.infiniteJetpackFuel || g_config.noWeightLimit ||
            g_config.superItemMagnet || g_config.autoPickup ||
            g_config.setScrapValue || g_config.autoReward;
        bool anyPrev = g_r.prevUnlimitedFlashlight || g_r.prevUnlimitedUsage ||
            g_r.prevInfiniteJetpackFuel || g_r.prevNoWeightLimit ||
            g_r.prevSuperItemMagnet || g_r.prevAutoPickup ||
            g_r.prevSetScrapValue;

        if (anyActive || anyPrev) {

    // ── Unlimited Flashlight ──────────────────────────────────────
    if (g_config.unlimitedFlashlight || g_r.prevUnlimitedFlashlight) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("BatterySlider");

        // ON→OFF: restore all flashlight battery fields
        if (!g_config.unlimitedFlashlight && g_r.prevUnlimitedFlashlight && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* bs = arr->At(i);
                if (!bs) continue;
                if (g_flashWasteSpeed  >= 0 && std::isfinite(g_r.origFlashWasteSpeed)) SetFloatIfChanged(bs, g_flashWasteSpeed, g_r.origFlashWasteSpeed);
                if (g_flashDischarge   >= 0)                                            SetU8IfChanged(bs, g_flashDischarge, g_r.origFlashDischarge);
                if (g_flashBatteryField >= 0 && g_sliderValue >= 0 && std::isfinite(g_r.origSliderValue)) {
                    void* slider = AsClass(bs)->GetMemberValue<void*>(g_flashBatteryField);
                    if (slider) SetFloatIfChanged(slider, g_sliderValue, g_r.origSliderValue);
                }
            }
        }

        if (g_config.unlimitedFlashlight && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* bs = arr->At(i);
                if (!bs) continue;

                // OFF→ON: save originals from the first object we find
                if (!g_r.prevUnlimitedFlashlight && i == 0) {
                    if (g_flashWasteSpeed  >= 0) g_r.origFlashWasteSpeed = AsClass(bs)->GetMemberValue<float>(g_flashWasteSpeed);
                    if (g_flashDischarge   >= 0) g_r.origFlashDischarge  = AsClass(bs)->GetMemberValue<uint8_t>(g_flashDischarge);
                    if (g_flashBatteryField >= 0 && g_sliderValue >= 0) {
                        void* slider = AsClass(bs)->GetMemberValue<void*>(g_flashBatteryField);
                        if (slider) g_r.origSliderValue = AsClass(slider)->GetMemberValue<float>(g_sliderValue);
                    }
                }

                if (g_flashWasteSpeed >= 0) SetFloatIfChanged(bs, g_flashWasteSpeed, 0.0f);
                if (g_flashDischarge  >= 0) SetU8IfChanged(bs, g_flashDischarge, 0);
                if (g_flashBatteryField >= 0 && g_sliderValue >= 0) {
                    void* slider = AsClass(bs)->GetMemberValue<void*>(g_flashBatteryField);
                    if (slider) SetFloatIfChanged(slider, g_sliderValue, 1.0f);
                }
            }
        }

        g_r.prevUnlimitedFlashlight = g_config.unlimitedFlashlight;
    }

    // ── Unlimited Usage + Infinite Jetpack Fuel ───────────────────
    if (g_config.unlimitedUsage || g_config.infiniteJetpackFuel ||
        g_r.prevUnlimitedUsage || g_r.prevInfiniteJetpackFuel)
    {
        bool anyElec = g_config.unlimitedUsage || g_config.infiniteJetpackFuel;
        auto* arr = Unity::Object::FindObjectsOfType<void>("ElectricItem");

        // ON→OFF: restore all electric item fields
        if (!anyElec && (g_r.prevUnlimitedUsage || g_r.prevInfiniteJetpackFuel) && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* ei = arr->At(i);
                if (!ei) continue;
                if (g_elecCapacity       >= 0 && std::isfinite(g_r.origElecCapacity))       SetFloatIfChanged(ei, g_elecCapacity, g_r.origElecCapacity);
                if (g_elecDischargeSpeed >= 0 && std::isfinite(g_r.origElecDischargeSpeed)) SetFloatIfChanged(ei, g_elecDischargeSpeed, g_r.origElecDischargeSpeed);
                if (g_elecIsDischarged   >= 0)                                               SetU8IfChanged(ei, g_elecIsDischarged, g_r.origElecIsDischarged);
            }
        }

        if (anyElec && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* ei = arr->At(i);
                if (!ei) continue;

                // OFF→ON: save originals from first object
                if (!(g_r.prevUnlimitedUsage || g_r.prevInfiniteJetpackFuel) && i == 0) {
                    if (g_elecCapacity       >= 0) g_r.origElecCapacity       = AsClass(ei)->GetMemberValue<float>(g_elecCapacity);
                    if (g_elecDischargeSpeed >= 0) g_r.origElecDischargeSpeed = AsClass(ei)->GetMemberValue<float>(g_elecDischargeSpeed);
                    if (g_elecIsDischarged   >= 0) g_r.origElecIsDischarged   = AsClass(ei)->GetMemberValue<uint8_t>(g_elecIsDischarged);
                }

                if (g_elecCapacity       >= 0) SetFloatIfChanged(ei, g_elecCapacity, 999999.0f);
                if (g_elecDischargeSpeed >= 0) SetFloatIfChanged(ei, g_elecDischargeSpeed, 0.0f);
                if (g_elecIsDischarged   >= 0) SetU8IfChanged(ei, g_elecIsDischarged, 0);
            }
        }

        g_r.prevUnlimitedUsage      = g_config.unlimitedUsage;
        g_r.prevInfiniteJetpackFuel = g_config.infiniteJetpackFuel;
    }

    // ── No Weight Limit ───────────────────────────────────────────
    if (g_config.noWeightLimit || g_r.prevNoWeightLimit) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("Inventory");
        void* inv = (arr && arr->m_uMaxLength > 0) ? arr->At(0) : nullptr;

        // ON→OFF: restore
        if (!g_config.noWeightLimit && g_r.prevNoWeightLimit && inv && g_invMaxWeight >= 0 && std::isfinite(g_r.origInvMaxWeight))
            SetFloatIfChanged(inv, g_invMaxWeight, g_r.origInvMaxWeight);
        else if (g_config.noWeightLimit && inv && g_invMaxWeight >= 0) {
            if (!g_r.prevNoWeightLimit)
                g_r.origInvMaxWeight = AsClass(inv)->GetMemberValue<float>(g_invMaxWeight);
            SetFloatIfChanged(inv, g_invMaxWeight, 500.0f); // subtle: still has a limit, just very generous
        }

        g_r.prevNoWeightLimit = g_config.noWeightLimit;
    }

    // ── Super Item Magnet + Auto Pickup ───────────────────────────
    if (g_config.superItemMagnet || g_config.autoPickup ||
        g_r.prevSuperItemMagnet || g_r.prevAutoPickup)
    {
        bool anyMagnet = g_config.superItemMagnet || g_config.autoPickup;
        auto* arr = Unity::Object::FindObjectsOfType<void>("ItemMagnet");

        // ON→OFF: restore magnet fields
        if (!anyMagnet && (g_r.prevSuperItemMagnet || g_r.prevAutoPickup) && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* mag = arr->At(i);
                if (!mag) continue;
                if (g_magnetStrength >= 0 && std::isfinite(g_r.origMagnetStrength))
                    SetFloatIfChanged(mag, g_magnetStrength, g_r.origMagnetStrength);
                if (g_magnetMaxItems >= 0 && g_r.origMagnetMaxItems != INT_MIN)
                    SetIntIfChanged(mag, g_magnetMaxItems, g_r.origMagnetMaxItems);
            }
        }

        if (anyMagnet && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* mag = arr->At(i);
                if (!mag) continue;

                // OFF→ON: save originals from first object
                if (!(g_r.prevSuperItemMagnet || g_r.prevAutoPickup) && i == 0) {
                    if (g_magnetStrength >= 0) g_r.origMagnetStrength = AsClass(mag)->GetMemberValue<float>(g_magnetStrength);
                    if (g_magnetMaxItems >= 0) g_r.origMagnetMaxItems = AsClass(mag)->GetMemberValue<int>(g_magnetMaxItems);
                }

                // More subtle magnet: 35/50 instead of 100/500
                float strength = g_config.autoPickup ? 50.0f : 35.0f;
                if (g_magnetStrength >= 0) SetFloatIfChanged(mag, g_magnetStrength, strength);
                if (g_config.autoPickup && g_magnetMaxItems >= 0)
                    SetIntIfChanged(mag, g_magnetMaxItems, 10); // More subtle: 10 instead of 99
            }
        }

        g_r.prevSuperItemMagnet = g_config.superItemMagnet;
        g_r.prevAutoPickup     = g_config.autoPickup;
    }

    // ── Set Scrap Value ───────────────────────────────────────────
    if (g_config.setScrapValue || g_r.prevSetScrapValue) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("InventoryItem");

        // ON→OFF: restore item values
        if (!g_config.setScrapValue && g_r.prevSetScrapValue && arr && g_r.origItemValue != INT_MIN) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* item = arr->At(i);
                if (!item) continue;
                if (g_itemValue >= 0) SetIntIfChanged(item, g_itemValue, g_r.origItemValue);
            }
        }

        if (g_config.setScrapValue && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* item = arr->At(i);
                if (!item) continue;
                // Save original value from first item
                if (!g_r.prevSetScrapValue && i == 0 && g_itemValue >= 0)
                    g_r.origItemValue = AsClass(item)->GetMemberValue<int>(g_itemValue);
                // More believable max scrap value: 999 instead of 9999
                if (g_itemValue >= 0) SetIntIfChanged(item, g_itemValue, 999);
            }
        }

        g_r.prevSetScrapValue = g_config.setScrapValue;
    }

    // ── Auto Reward Ads — bypass handled by hook on OnOnlineModeButtonPressed ──
    // The hook intercepts the button press and calls onAdComleted directly,
    // so FindObjectsOfType is NOT needed here. Just consume the flag.
    if (g_config.autoReward)
        g_config.autoReward = false;

        } // end if (anyActive || anyPrev)
    } // end slot 3 (Items)

    // ════════════════════════════════════════════════════════════════
    // COMBAT (Slot 4)
    // ════════════════════════════════════════════════════════════════
    if (g_slot == 4)
    {
        bool combatActive = g_config.oneHitKill || g_config.instantKillAll || g_config.blindEnemies;

        if (combatActive) {

    // ── One-Hit Kill + Instant Kill All ──
    if (g_config.oneHitKill || g_config.instantKillAll) {
        void* playerHealth = nullptr;
        auto* phArr = Unity::Object::FindObjectsOfType<void>("PlayerHealth");
        if (phArr && phArr->m_uMaxLength > 0) playerHealth = phArr->At(0);

        auto* healthArr = Unity::Object::FindObjectsOfType<void>("Health");
        if (healthArr) {
            for (uintptr_t i = 0; i < healthArr->m_uMaxLength; i++) {
                void* h = healthArr->At(i);
                if (!h || h == playerHealth) continue;
                if (g_config.oneHitKill && g_healthPoints >= 0)
                    SetFloatIfChanged(h, g_healthPoints, 0.0f);
                if (g_config.instantKillAll)
                    InvokeManaged(h, "Health", "Kill", 0, nullptr);
            }
        }
    }

    // ── Blind Enemies ──
    if (g_config.blindEnemies) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("EnemyBase");
        if (arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* e = arr->At(i);
                if (!e) continue;
                if (g_enemyNavMesh >= 0) SetPtr(e, g_enemyNavMesh, nullptr);
                if (g_enemyLocalPlayer >= 0) SetPtr(e, g_enemyLocalPlayer, nullptr);
            }
        }
    }

        } // end if (combatActive)
    } // end slot 4

    // ════════════════════════════════════════════════════════════════
    // FIND THROTTLE — limits expensive FindObjectsOfType calls to reduce GC pressure
    // ── Slot system already throttles per-slot hacks to every 5 frames.
    // ── This throttle applies to all remaining FindObjectsOfType calls below
    //    (forced hacks + traps/doors) to ~every 3 frames.
    // ════════════════════════════════════════════════════════════════
    bool canFind = (++g_findThrottle >= 3);
    if (canFind) g_findThrottle = 0;

    if (canFind) {

    // ── Quota Manipulation (with prev-state tracking for restore) ──
    if (g_config.quotaManipulation || g_r.prevQuotaManipulation) {
        auto* qArr = Unity::Object::FindObjectsOfType<void>("quota");
        void* q = (qArr && qArr->m_uMaxLength > 0) ? qArr->At(0) : nullptr;

        if (q && g_quotaDeadline >= 0) {
            if (!g_config.quotaManipulation && g_r.prevQuotaManipulation) {
                // ON→OFF: restore original values
                if (g_quotaTotalCollected >= 0 && g_r.origQuotaTotalCollected != INT_MIN)
                    SetIntIfChanged(q, g_quotaTotalCollected, g_r.origQuotaTotalCollected);
                if (g_quotaDeadline >= 0 && g_r.origQuotaDeadline != INT_MIN)
                    SetIntIfChanged(q, g_quotaDeadline, g_r.origQuotaDeadline);
            } else if (g_config.quotaManipulation) {
                if (!g_r.prevQuotaManipulation) {
                    // OFF→ON: save originals
                    if (g_quotaTotalCollected >= 0) g_r.origQuotaTotalCollected = AsClass(q)->GetMemberValue<int>(g_quotaTotalCollected);
                    if (g_quotaDeadline       >= 0) g_r.origQuotaDeadline       = AsClass(q)->GetMemberValue<int>(g_quotaDeadline);
                }
                // Per-frame write (belt + suspenders with the updateView hook above)
                if (g_quotaAmount >= 0 && g_quotaTotalCollected >= 0) {
                    int quotaAmt = AsClass(q)->GetMemberValue<int>(g_quotaAmount);
                    SetIntIfChanged(q, g_quotaTotalCollected, quotaAmt + 1000);
                }
                if (g_quotaDeadline >= 0)
                    SetIntIfChanged(q, g_quotaDeadline, 99);
            }
        }

        g_r.prevQuotaManipulation = g_config.quotaManipulation;
    }

    // ── Infinity Jumps (canJump save/restore — throttled) ──
    if (g_config.infinityJumps || g_r.prevInfinityJumps) {
        auto* fpArr = Unity::Object::FindObjectsOfType<void>("FP_Controller");
        void* fp = (fpArr && fpArr->m_uMaxLength > 0) ? fpArr->At(0) : nullptr;

        if (fp) {
            // ON→OFF: restore canJump only
            if (!g_config.infinityJumps && g_r.prevInfinityJumps) {
                if (g_fpCanJump >= 0)
                    SetBoolIfChanged(fp, g_fpCanJump, g_r.origCanJump);
            }
            // ON: set canJump
            else if (g_config.infinityJumps) {
                if (!g_r.prevInfinityJumps && g_fpCanJump >= 0)
                    g_r.origCanJump = AsClass(fp)->GetMemberValue<bool>(g_fpCanJump);
                if (g_fpCanJump >= 0)
                    SetBoolIfChanged(fp, g_fpCanJump, true);
            }
        }
        g_r.prevInfinityJumps = g_config.infinityJumps;
    }

    // ════════════════════════════════════════════════════════════════
    // TRAPS & DOORS (also throttled by findThrottle to reduce GC)
    // ════════════════════════════════════════════════════════════════

    // ── Instant Doors ─────────────────────────────────────────────
    if (g_config.instantDoors || g_r.prevInstantDoors) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("OpenDoorTrigger");

        // ON→OFF: restore door open timer + coroutine fields
        if (!g_config.instantDoors && g_r.prevInstantDoors && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* door = arr->At(i);
                if (!door) continue;
                if (g_doorSeconds >= 0 && std::isfinite(g_r.origDoorSeconds))
                    SetFloatIfChanged(door, g_doorSeconds, g_r.origDoorSeconds);
            }
        }

        if (g_config.instantDoors && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* door = arr->At(i);
                if (!door) continue;

                if (!g_r.prevInstantDoors && i == 0 && g_doorSeconds >= 0)
                    g_r.origDoorSeconds = AsClass(door)->GetMemberValue<float>(g_doorSeconds);

                if (g_doorSeconds >= 0)
                    SetFloatIfChanged(door, g_doorSeconds, 0.0f);

                InvokeManaged(door, "OpenDoorTrigger", "StopAllCoroutines", 0, nullptr);
            }
        }

        g_r.prevInstantDoors = g_config.instantDoors;
    }

    // ── Disable Mines ─────────────────────────────────────────────
    if (g_config.disableMines || g_r.prevDisableMines) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("MineTrigger");

        if (!g_config.disableMines && g_r.prevDisableMines && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* mine = arr->At(i);
                if (mine && g_mineExploded >= 0)
                    SetU8IfChanged(mine, g_mineExploded, g_r.origMineExploded);
            }
        }

        if (g_config.disableMines && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* mine = arr->At(i);
                if (!mine) continue;
                if (!g_r.prevDisableMines && i == 0 && g_mineExploded >= 0)
                    g_r.origMineExploded = AsClass(mine)->GetMemberValue<uint8_t>(g_mineExploded);
                if (g_mineExploded >= 0)
                    SetU8IfChanged(mine, g_mineExploded, 1);
            }
        }

        g_r.prevDisableMines = g_config.disableMines;
    }

    // ── Ship Door Always Open ─────────────────────────────────────
    if (g_config.shipDoorAlwaysOpen || g_r.prevShipDoorAlwaysOpen) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("ShipDoorController");

        if (!g_config.shipDoorAlwaysOpen && g_r.prevShipDoorAlwaysOpen && arr && g_r.origShipDoorPower != INT_MIN) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* door = arr->At(i);
                if (door && g_shipDoorPower >= 0)
                    SetIntIfChanged(door, g_shipDoorPower, g_r.origShipDoorPower);
            }
        }

        if (g_config.shipDoorAlwaysOpen && arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* door = arr->At(i);
                if (!door) continue;
                if (!g_r.prevShipDoorAlwaysOpen && i == 0 && g_shipDoorPower >= 0)
                    g_r.origShipDoorPower = AsClass(door)->GetMemberValue<int>(g_shipDoorPower);
                if (g_shipDoorPower >= 0)
                    SetIntIfChanged(door, g_shipDoorPower, 100);
            }
        }

        g_r.prevShipDoorAlwaysOpen = g_config.shipDoorAlwaysOpen;
    }

    // ── No Teleporter Cooldown ────────────────────────────────────
    if (g_config.noTeleporterCooldown) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("TeleporterButton");
        if (arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* tp = arr->At(i);
                if (!tp) continue;
                if (g_teleTimeout >= 0 && g_teleTimePassed >= 0) {
                    int timeout = AsClass(tp)->GetMemberValue<int>(g_teleTimeout);
                    if (timeout > 0)
                        SetIntIfChanged(tp, g_teleTimePassed, timeout);
                }
            }
        }
    } // end if (noTeleporterCooldown)

    } // end if (canFind) — throttled FindObjectsOfType block

    // ════════════════════════════════════════════════════════════════
    // PER-FRAME HACKS (outside findThrottle — run every ApplyCheats call)
    // ════════════════════════════════════════════════════════════════

    // ── Infinity Jumps: force grounded=true ──
    // Physics resets grounded every frame, so this must run every ApplyCheats call.
    if (g_config.infinityJumps && g_fpGrounded >= 0) {
        auto* fpArr = Unity::Object::FindObjectsOfType<void>("FP_Controller");
        void* fp = (fpArr && fpArr->m_uMaxLength > 0) ? fpArr->At(0) : nullptr;
        if (fp)
            AsClass(fp)->SetMemberValue<bool>(g_fpGrounded, true);
    }

    // ════════════════════════════════════════════════════════════════
    // ECONOMY — aggressive per-frame writes (outside all throttles)
    // TerminalShop values are SyncVar'd and reset aggressively by the server.
    // Per-frame force-writes ensure they stay at our target values.
    // ════════════════════════════════════════════════════════════════

    // ── Unlimited Money — aggressive per-frame ──
    if (g_config.unlimitedMoney || g_r.prevUnlimitedMoney) {
        auto* shopArr = Unity::Object::FindObjectsOfType<void>("TerminalShop");
        void* shop = (shopArr && shopArr->m_uMaxLength > 0) ? shopArr->At(0) : nullptr;

        if (shop && g_shopMoney >= 0) {
            if (g_config.unlimitedMoney) {
                if (!g_r.prevUnlimitedMoney) {
                    // One-shot CmdAddMoney on enable — sets server money high once
                    int addAmount = 100000;
                    void* args[1] = { &addAmount };
                    InvokeManaged(shop, "TerminalShop", "CmdAddMoney", 1, args);
                    LOGD("Money: enabled + one-shot CmdAddMoney(%d)", addAmount);
                }
                // AGGRESSIVE: force-write every ApplyCheats call
                int cur = AsClass(shop)->GetMemberValue<int>(g_shopMoney);
                if (cur != 99999)
                    AsClass(shop)->SetMemberValue<int>(g_shopMoney, 99999);
            }
        }
        g_r.prevUnlimitedMoney = g_config.unlimitedMoney;
    }

    // ── Free Items — aggressive per-frame ──
    if (g_config.freeItems || g_r.prevFreeItems) {
        auto* shopArr = Unity::Object::FindObjectsOfType<void>("TerminalShop");
        void* shop = (shopArr && shopArr->m_uMaxLength > 0) ? shopArr->At(0) : nullptr;

        if (shop && g_shopTotalPrice >= 0) {
            if (g_config.freeItems) {
                // AGGRESSIVE: force-write every ApplyCheats call
                int cur = AsClass(shop)->GetMemberValue<int>(g_shopTotalPrice);
                if (cur != 0)
                    AsClass(shop)->SetMemberValue<int>(g_shopTotalPrice, 0);
            }
        }
        g_r.prevFreeItems = g_config.freeItems;
    }
}

// ════════════════════════════════════════════════════════════════════
// APPLY ECONOMY CHEATS (render thread — called from eglSwapBuffers hook)
// ════════════════════════════════════════════════════════════════════

void ApplyEconomyCheats() {
    if (!IL2CPP::Globals.m_GameAssembly) return;
    if (g_inTransition) return; // skip during scene loads — FindObjectsOfType unsafe

    // Ensure offsets are resolved (does nothing if already resolved)
    ResolveOffsets();

    // Setup economy hooks (guarded by static flags — one-shot)
    SetupEconomyHooks();

    // ── Field writes: aggressive per-frame from render thread ──
    // Writes money and total price every call so the shop always shows our values.
    if (g_config.unlimitedMoney || g_config.freeItems) {
        auto* shopArr = Unity::Object::FindObjectsOfType<void>("TerminalShop");
        void* shop = (shopArr && shopArr->m_uMaxLength > 0) ? shopArr->At(0) : nullptr;
        if (shop) {
            if (g_config.unlimitedMoney && g_shopMoney >= 0) {
                int cur = AsClass(shop)->GetMemberValue<int>(g_shopMoney);
                if (cur != 99999)
                    AsClass(shop)->SetMemberValue<int>(g_shopMoney, 99999);
            }
            if (g_config.freeItems && g_shopTotalPrice >= 0) {
                int cur = AsClass(shop)->GetMemberValue<int>(g_shopTotalPrice);
                if (cur != 0)
                    AsClass(shop)->SetMemberValue<int>(g_shopTotalPrice, 0);
            }
        }
    }
    if (g_config.quotaManipulation) {
        auto* qArr = Unity::Object::FindObjectsOfType<void>("quota");
        void* q = (qArr && qArr->m_uMaxLength > 0) ? qArr->At(0) : nullptr;
        if (q && g_quotaDeadline >= 0 && g_quotaTotalCollected >= 0 && g_quotaAmount >= 0) {
            AsClass(q)->SetMemberValue<int>(g_quotaDeadline, 99);
            int quotaAmt = AsClass(q)->GetMemberValue<int>(g_quotaAmount);
            AsClass(q)->SetMemberValue<int>(g_quotaTotalCollected, quotaAmt + 1000);
        }
    }
}
