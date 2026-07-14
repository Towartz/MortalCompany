#include "cheats.h"
#include <map>
#include "Includes/obfuscate.h"
#include "Includes/Logger.h"
#include "Includes/Utils.hpp"
#include "KittyMemory/KittyInclude.hpp"
#include "Includes/Macros.h"
#include "dobby.h"
#include <cmath>

#define targetLibName OBFUSCATE("libil2cpp.so")
#define MONEY_TARGET 1000000

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
static inline void SetPtr(void* obj, int offset, void* target) {
    if (offset < 0) return;
    AsClass(obj)->SetMemberValue<void*>(offset, target);
}

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
int g_adsShowedAd        = -1;
int g_adsOpenOnlineMode  = -1;
static int g_cfgOnlineModeActive = -1;
bool g_requestBypassReward = false;
bool g_requestSwitchOnline = false;
static int g_configShowAdsDuring = -1;
static int g_configShowAdsBefore = -1;
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
static int g_fpMoveDir          = -1;
static int g_fpJumpTimer        = -1;
static int g_fpAntiBunnyHop     = -1;
static int g_shopMoney          = -1;
static int g_shopTotalPrice     = -1;
static int g_quotaDeadline      = -1;
static int g_quotaAmount        = -1;
static int g_quotaTotalCollected = -1;
static int g_itemValue          = -1;
static int g_itemWeight         = -1;
static int g_itemIsHeavy        = -1;
static int g_itemName           = -1;
static int g_mineExploded       = -1;
static int g_shipDoorPower      = -1;
static int g_teleTimeout        = -1;
static int g_teleTimePassed     = -1;
static int g_clientsMyIdentity  = -1;
static int g_clientsList        = -1;
static int g_clientInfoIdentity = -1;
static int g_clientsControls     = -1;
static int g_clientsNetworkChat  = -1;
static int g_clientsKickedIds    = -1;
static int g_controlIdentity      = -1;
static int g_clientInfoNetId     = -1;
static int g_clientInfoPlayerId  = -1;

// ── Full Bright (RenderSettings ambient) ──
static void* g_setAmbientMode    = nullptr;
static void* g_setAmbientSky     = nullptr;
static void* g_setAmbientEquator = nullptr;
static void* g_setFogColor       = nullptr;
static void* g_setFogDensity     = nullptr;
static int   g_lightIntensity    = -1;
static bool  g_fbApplied         = false;
static float g_fbOrigFogDensity  = -1.0f;

// ── Auto reward: directly give the online-mode reward without watching an ad ──
void AutoReward_GiveReward() {
    auto* arr = Unity::Object::FindObjectsOfType<void>("RewardedAdsForOnline");
    if (!arr) return;
    for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
        void* ad = arr->At(i);
        if (!ad) continue;
        bool showed = (g_adsShowedAd >= 0)
            ? AsClass(ad)->GetMemberValue<bool>(g_adsShowedAd)
            : false;
        if (showed) continue;
        // Call onAdComleted FIRST while showedAd is still false
        // (method likely guards on !showedAd internally)
        InvokeManaged(ad, "RewardedAdsForOnline", "onAdComleted", 0, nullptr);
        if (g_adsShowedAd >= 0)
            AsClass(ad)->SetMemberValue<bool>(g_adsShowedAd, true);
        if (g_adsOpenOnlineMode >= 0) {
            void* evt = AsClass(ad)->GetMemberValue<void*>(g_adsOpenOnlineMode);
            if (evt)
                InvokeManaged(evt, "UnityEngine.Events.UnityEvent", "Invoke", 0, nullptr);
        }
        LOGD("autoReward: gave reward on instance %d", (int)i);
    }
}

// ── Directly enter online mode, bypassing the remote config / ad restriction ──
// Online mode is gated server-side: ConfigDownLoader.OnlineModeActive must be
// true (set from a remote JSON), otherwise OnlineModeLimiter blocks Play Online
// (and with no ad SDK the "watch ad" icon never appears). Force the flag on and
// fire the real OnOnlineModeButtonPressed() so the game's own flow runs.
void AutoReward_SwitchToOnline() {
    // Force the server-driven master switch for online mode to true.
    if (g_cfgOnlineModeActive >= 0) {
        auto* cfgArr = Unity::Object::FindObjectsOfType<void>("ConfigDownLoader");
        if (cfgArr) {
            for (uintptr_t i = 0; i < cfgArr->m_uMaxLength; i++) {
                void* cfg = cfgArr->At(i);
                if (cfg) AsClass(cfg)->SetMemberValue<bool>(g_cfgOnlineModeActive, true);
            }
        }
    }

    auto* arr = Unity::Object::FindObjectsOfType<void>("RewardedAdsForOnline");
    if (!arr) {
        LOGD("switchToOnline: RewardedAdsForOnline not found");
        return;
    }
    for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
        void* ad = arr->At(i);
        if (!ad) continue;
        // Call the actual button handler — it performs the online-mode transition.
        InvokeManaged(ad, "RewardedAdsForOnline", "OnOnlineModeButtonPressed", 0, nullptr);
        // Fallback: if the handler relies on the openOnlineMode event, fire it too.
        if (g_adsOpenOnlineMode >= 0) {
            void* evt = AsClass(ad)->GetMemberValue<void*>(g_adsOpenOnlineMode);
            if (evt)
                InvokeManaged(evt, "UnityEngine.Events.UnityEvent", "Invoke", 0, nullptr);
        }
        LOGD("switchToOnline: entered online mode on instance %d", (int)i);
    }
}

static bool g_resolved = false;
static bool g_inTransition = false;
static int g_transitionCooldown = 0;

static bool g_moneyHookSetup = false;
typedef void (*MoneyChanged_t)(void*, int, int, void*);
static MoneyChanged_t orig_onMoneyAmountChanged = nullptr;

static void my_onMoneyAmountChanged(void* shop, int oldValue, int newValue, void* method) {
    if (g_config.unlimitedMoney && g_shopMoney >= 0) {
        if (newValue < 90000) {
            AsClass(shop)->SetMemberValue<int>(g_shopMoney, 99999);
            if (orig_onMoneyAmountChanged)
                orig_onMoneyAmountChanged(shop, oldValue, 99999, method);
            return;
        }
    }
    if (orig_onMoneyAmountChanged)
        orig_onMoneyAmountChanged(shop, oldValue, newValue, method);
}

static bool g_totalPriceHookSetup = false;
typedef void (*CalcPrice_t)(void*, void*);
static CalcPrice_t orig_CalculateTotalPrice = nullptr;

static void my_CalculateTotalPrice(void* shop, void* method) {
    if (orig_CalculateTotalPrice)
        orig_CalculateTotalPrice(shop, method);
    if (g_config.freeItems && g_shopTotalPrice >= 0)
        AsClass(shop)->SetMemberValue<int>(g_shopTotalPrice, 0);
}

static bool g_elecCapHookSetup = false;
typedef void (*SetNetFloat_t)(void*, float, void*);
static SetNetFloat_t orig_set_Networkcapacity = nullptr;

static void my_set_Networkcapacity(void* ei, float serverValue, void* method) {
    bool elecActive = g_config.unlimitedUsage || g_config.infiniteJetpackFuel;
    if (elecActive && g_elecCapacity >= 0 && serverValue < 999999.0f) {
        if (orig_set_Networkcapacity)
            orig_set_Networkcapacity(ei, 999999.0f, method);
        return;
    }
    if (orig_set_Networkcapacity)
        orig_set_Networkcapacity(ei, serverValue, method);
}

static bool g_elecDiscHookSetup = false;
typedef void (*SetNetBool_t)(void*, bool, void*);
static SetNetBool_t orig_set_NetworkisDiscarged = nullptr;

static void my_set_NetworkisDiscarged(void* ei, bool serverValue, void* method) {
    bool elecActive = g_config.unlimitedUsage || g_config.infiniteJetpackFuel;
    if (elecActive && g_elecIsDischarged >= 0 && serverValue) {
        if (orig_set_NetworkisDiscarged)
            orig_set_NetworkisDiscarged(ei, false, method);
        return;
    }
    if (orig_set_NetworkisDiscarged)
        orig_set_NetworkisDiscarged(ei, serverValue, method);
}

static bool g_teleHookSetup = false;
typedef void (*SetNetTimePassed_t)(void*, int, void*);
static SetNetTimePassed_t orig_set_NetworktimePassed = nullptr;

static void my_set_NetworktimePassed(void* tp, int serverValue, void* method) {
    if (g_config.noTeleporterCooldown && g_teleTimeout >= 0 && g_teleTimePassed >= 0) {
        int timeout = AsClass(tp)->GetMemberValue<int>(g_teleTimeout);
        if (timeout > 0 && serverValue < timeout) {
            if (orig_set_NetworktimePassed)
                orig_set_NetworktimePassed(tp, timeout, method);
            return;
        }
    }
    if (orig_set_NetworktimePassed)
        orig_set_NetworktimePassed(tp, serverValue, method);
}

static bool g_scrapHookSetup = false;
typedef void (*SetNetIsActive_t)(void*, bool, void*);
static SetNetIsActive_t orig_set_NetworkisActive = nullptr;

static void my_set_NetworkisActive(void* item, bool value, void* method) {
    if (orig_set_NetworkisActive)
        orig_set_NetworkisActive(item, value, method);
    if (g_config.setScrapValue && g_itemValue >= 0)
        AsClass(item)->SetMemberValue<int>(g_itemValue, 999);
}

static bool g_quotaHookSetup = false;
typedef void (*UpdateView_t)(void*, int, int, void*);
static UpdateView_t orig_quota_updateView = nullptr;

static void my_quota_updateView(void* q, int oldValue, int newValue, void* method) {
    if (g_config.quotaManipulation && g_quotaDeadline >= 0 && g_quotaTotalCollected >= 0 && g_quotaAmount >= 0) {
        AsClass(q)->SetMemberValue<int>(g_quotaDeadline, 99);
        int quotaAmt = AsClass(q)->GetMemberValue<int>(g_quotaAmount);
        AsClass(q)->SetMemberValue<int>(g_quotaTotalCollected, quotaAmt + 1000);
    }
    if (orig_quota_updateView)
        orig_quota_updateView(q, oldValue, newValue, method);
}

// ── Anti-Kick: block ClientsInLobby::AddPlayerToKickList when target is us ──
static bool g_antiKickHookSetup = false;
typedef void (*AddKick_t)(void*, void*, void*);
static AddKick_t orig_AddPlayerToKickList = nullptr;

static void my_AddPlayerToKickList(void* clients, void* identity, void* method) {
    if (g_config.antiKick && clients && g_clientsMyIdentity >= 0) {
        void* myId = AsClass(clients)->GetMemberValue<void*>(g_clientsMyIdentity);
        if (identity && identity == myId) {
            LOGD("AntiKick: blocked kick on self");
            return;
        }
    }
    if (orig_AddPlayerToKickList)
        orig_AddPlayerToKickList(clients, identity, method);
}

// ── Anti-Ban / Anti-Disconnect ──
// Register-forwarding typedefs cover value-type/struct args passed by
// pointer (AAPCS64) plus the trailing MethodInfo* — safe to forward blindly.
static bool g_antiBanHookSetup = false;
typedef void (*OnMsg_t)(void*, void*, void*, void*, void*, void*);
static OnMsg_t orig_OnMessageRecive = nullptr;
static void my_OnMessageRecive(void* self, void* a1, void* a2, void* a3, void* a4, void* a5) {
    if (g_config.antiBan) {
        LOGD("AntiBan: dropped incoming ServerMessage");
        return;
    }
    if (orig_OnMessageRecive)
        orig_OnMessageRecive(self, a1, a2, a3, a4, a5);
}

typedef void (*ShowPopup_t)(void*, void*, void*);
static ShowPopup_t orig_showDisconnectPopUp = nullptr;
static void my_showDisconnectPopUp(void* self, void* msg, void* method) {
    if (g_config.antiBan) {
        LOGD("AntiBan: suppressed disconnect popup");
        return;
    }
    if (orig_showDisconnectPopUp)
        orig_showDisconnectPopUp(self, msg, method);
}

typedef void (*TargetDisc_t)(void*, void*, void*, void*);
static TargetDisc_t orig_TargetShowDisconnectMessage = nullptr;
static void my_TargetShowDisconnectMessage(void* self, void* conn, void* msg, void* method) {
    if (g_config.antiBan) {
        LOGD("AntiBan: ignored TargetShowDisconnectMessage");
        return;
    }
    if (orig_TargetShowDisconnectMessage)
        orig_TargetShowDisconnectMessage(self, conn, msg, method);
}

static struct {
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

static void LogOffset(const char* cls, const char* field, int off) {
    if (off < 0)
        LOGD("OFFSET MISS: %s::%s = %d", cls, field, off);
}

void ResolveOffsets() {
    if (g_resolved) return;
    g_staminaWasteSpeed  = GetFieldOffset("Stamina", "wasteSpeed");        LogOffset("Stamina","wasteSpeed",g_staminaWasteSpeed);
    g_staminaGainSpeed   = GetFieldOffset("Stamina", "gainSpeed");         LogOffset("Stamina","gainSpeed",g_staminaGainSpeed);
    g_staminaCanRun      = GetFieldOffset("Stamina", "canRun");            LogOffset("Stamina","canRun",g_staminaCanRun);
    g_healthPoints       = GetFieldOffset("Health", "healthPoints");       LogOffset("Health","healthPoints",g_healthPoints);
    g_healthIsDead       = GetFieldOffset("Health", "isDead");             LogOffset("Health","isDead",g_healthIsDead);
    g_healthUndamage     = GetFieldOffset("Health", "UndamageAble");       LogOffset("Health","UndamageAble",g_healthUndamage);
    g_flashWasteSpeed    = GetFieldOffset("BatterySlider", "wasteSpeed");  LogOffset("BatterySlider","wasteSpeed",g_flashWasteSpeed);
    g_flashDischarge     = GetFieldOffset("BatterySlider", "discharge");   LogOffset("BatterySlider","discharge",g_flashDischarge);
    g_flashBatteryField  = GetFieldOffset("BatterySlider", "Battery");     LogOffset("BatterySlider","Battery",g_flashBatteryField);
    g_sliderValue        = GetFieldOffset("Slider", "m_Value");            LogOffset("Slider","m_Value",g_sliderValue);
    g_elecCapacity       = GetFieldOffset("ElectricItem", "capacity");     LogOffset("ElectricItem","capacity",g_elecCapacity);
    g_elecDischargeSpeed = GetFieldOffset("ElectricItem", "dischargeSpeed"); LogOffset("ElectricItem","dischargeSpeed",g_elecDischargeSpeed);
    g_elecIsDischarged   = GetFieldOffset("ElectricItem", "isDiscarged");  LogOffset("ElectricItem","isDiscarged",g_elecIsDischarged);
    g_adsShowedAd        = GetFieldOffset("RewardedAdsForOnline", "showedAd"); LogOffset("RewardedAdsForOnline","showedAd",g_adsShowedAd);
    g_adsOpenOnlineMode  = GetFieldOffset("RewardedAdsForOnline", "openOnlineMode"); LogOffset("RewardedAdsForOnline","openOnlineMode",g_adsOpenOnlineMode);
    g_cfgOnlineModeActive = GetFieldOffset("ConfigDownLoader", "OnlineModeActive"); LogOffset("ConfigDownLoader","OnlineModeActive",g_cfgOnlineModeActive);
    g_configShowAdsDuring = GetFieldOffset("ConfigDownLoader", "<ShowAdsDuringGame>k__BackingField"); LogOffset("ConfigDownLoader","ShowAdsDuringGame",g_configShowAdsDuring);
    g_configShowAdsBefore = GetFieldOffset("ConfigDownLoader", "<ShowAdsBeforeGame>k__BackingField"); LogOffset("ConfigDownLoader","ShowAdsBeforeGame",g_configShowAdsBefore);
    g_fpWalkSpeed        = GetFieldOffset("FP_Controller", "walkSpeed");   LogOffset("FP_Controller","walkSpeed",g_fpWalkSpeed);
    g_fpRunSpeed         = GetFieldOffset("FP_Controller", "runSpeed");    LogOffset("FP_Controller","runSpeed",g_fpRunSpeed);
    g_fpJumpForce        = GetFieldOffset("FP_Controller", "jumpForce");   LogOffset("FP_Controller","jumpForce",g_fpJumpForce);
    g_fpGravity          = GetFieldOffset("FP_Controller", "gravity");     LogOffset("FP_Controller","gravity",g_fpGravity);
    g_fpAirControl       = GetFieldOffset("FP_Controller", "airControl");  LogOffset("FP_Controller","airControl",g_fpAirControl);
    g_fpFallDamageVel    = GetFieldOffset("FP_Controller", "velocityToTakeDamage"); LogOffset("FP_Controller","velocityToTakeDamage",g_fpFallDamageVel);
    g_invMaxWeight       = GetFieldOffset("Inventory", "maxWeightToCarry"); LogOffset("Inventory","maxWeightToCarry",g_invMaxWeight);
    g_magnetStrength     = GetFieldOffset("ItemMagnet", "attractionStrength"); LogOffset("ItemMagnet","attractionStrength",g_magnetStrength);
    g_magnetMaxItems     = GetFieldOffset("ItemMagnet", "maxItemsAmountToAttract"); LogOffset("ItemMagnet","maxItemsAmountToAttract",g_magnetMaxItems);
    g_doorSeconds        = GetFieldOffset("OpenDoorTrigger", "secondsToOpenDoor"); LogOffset("OpenDoorTrigger","secondsToOpenDoor",g_doorSeconds);
    g_doorDelayBuffer    = GetFieldOffset("OpenDoorTrigger", "delayBuffer"); LogOffset("OpenDoorTrigger","delayBuffer",g_doorDelayBuffer);
    g_doorStopDelayCor   = GetFieldOffset("OpenDoorTrigger", "stopDelayCoroutine"); LogOffset("OpenDoorTrigger","stopDelayCoroutine",g_doorStopDelayCor);
    g_enemyLocalPlayer   = GetFieldOffset("EnemyBase", "LocalPlayer");     LogOffset("EnemyBase","LocalPlayer",g_enemyLocalPlayer);
    g_enemyNavMesh       = GetFieldOffset("EnemyBase", "navMeshAgent");    LogOffset("EnemyBase","navMeshAgent",g_enemyNavMesh);
    g_enemyActivePlayers = GetFieldOffset("EnemyBase", "ActivePlayers");   LogOffset("EnemyBase","ActivePlayers",g_enemyActivePlayers);
    g_shopMoney          = GetFieldOffset("TerminalShop", "Money");         LogOffset("TerminalShop","Money",g_shopMoney);
    g_shopTotalPrice     = GetFieldOffset("TerminalShop", "TotalPrice");   LogOffset("TerminalShop","TotalPrice",g_shopTotalPrice);
    g_quotaDeadline      = GetFieldOffset("quota", "deadline");            LogOffset("quota","deadline",g_quotaDeadline);
    g_quotaAmount        = GetFieldOffset("quota", "quotaAmount");         LogOffset("quota","quotaAmount",g_quotaAmount);
    g_quotaTotalCollected = GetFieldOffset("quota", "totalCollected");     LogOffset("quota","totalCollected",g_quotaTotalCollected);
    g_itemValue          = GetFieldOffset("InventoryItem", "Value");       LogOffset("InventoryItem","Value",g_itemValue);
    g_itemWeight         = GetFieldOffset("InventoryItem", "Weight");     LogOffset("InventoryItem","Weight",g_itemWeight);
    g_itemIsHeavy        = GetFieldOffset("InventoryItem", "isHeavy");    LogOffset("InventoryItem","isHeavy",g_itemIsHeavy);
    g_itemName           = GetFieldOffset("InventoryItem", "itemName");   LogOffset("InventoryItem","itemName",g_itemName);
    g_mineExploded       = GetFieldOffset("MineTrigger", "alreadyExploded"); LogOffset("MineTrigger","alreadyExploded",g_mineExploded);
    g_shipDoorPower      = GetFieldOffset("ShipDoorController", "doorPower"); LogOffset("ShipDoorController","doorPower",g_shipDoorPower);
    g_teleTimeout        = GetFieldOffset("TeleporterButton", "timeout");   LogOffset("TeleporterButton","timeout",g_teleTimeout);
    g_teleTimePassed     = GetFieldOffset("TeleporterButton", "timePassed"); LogOffset("TeleporterButton","timePassed",g_teleTimePassed);
    g_clientsMyIdentity  = GetFieldOffset("ClientsInLobby", "myIdentity");   LogOffset("ClientsInLobby","myIdentity",g_clientsMyIdentity);
    g_clientsList        = GetFieldOffset("ClientsInLobby", "Clients");      LogOffset("ClientsInLobby","Clients",g_clientsList);
    g_clientInfoIdentity = GetFieldOffset("ClientInfo", "networkIdentity");  LogOffset("ClientInfo","networkIdentity",g_clientInfoIdentity);
    g_clientsControls    = GetFieldOffset("ClientsInLobby", "clientInfoControlls"); LogOffset("ClientsInLobby","clientInfoControlls",g_clientsControls);
    g_clientsNetworkChat = GetFieldOffset("ClientsInLobby", "networkChat");  LogOffset("ClientsInLobby","networkChat",g_clientsNetworkChat);
    g_clientsKickedIds   = GetFieldOffset("ClientsInLobby", "kickedPlayersIds"); LogOffset("ClientsInLobby","kickedPlayersIds",g_clientsKickedIds);
    g_controlIdentity      = GetFieldOffset("ClientInfoControll", "identity"); LogOffset("ClientInfoControll","identity",g_controlIdentity);
    g_clientInfoNetId      = GetFieldOffset("ClientInfo", "networkIdentity"); LogOffset("ClientInfo","networkIdentity",g_clientInfoNetId);
    g_clientInfoPlayerId   = GetFieldOffset("ClientInfo", "playerId"); LogOffset("ClientInfo","playerId",g_clientInfoPlayerId);

    g_setAmbientMode    = IL2CPP::Class::Utils::GetMethodPointer("UnityEngine.RenderSettings", "set_ambientMode", 1);
    g_setAmbientSky     = IL2CPP::Class::Utils::GetMethodPointer("UnityEngine.RenderSettings", "set_ambientSkyColor", 1);
    if (!g_setAmbientSky) g_setAmbientSky = IL2CPP::Class::Utils::GetMethodPointer("UnityEngine.RenderSettings", "set_ambientSkyColor_Injected", 1);
    g_setAmbientEquator = IL2CPP::Class::Utils::GetMethodPointer("UnityEngine.RenderSettings", "set_ambientEquatorColor", 1);
    if (!g_setAmbientEquator) g_setAmbientEquator = IL2CPP::Class::Utils::GetMethodPointer("UnityEngine.RenderSettings", "set_ambientEquatorColor_Injected", 1);
    g_setFogColor       = IL2CPP::Class::Utils::GetMethodPointer("UnityEngine.RenderSettings", "set_fogColor", 1);
    if (!g_setFogColor) g_setFogColor = IL2CPP::Class::Utils::GetMethodPointer("UnityEngine.RenderSettings", "set_fogColor_Injected", 1);
    g_setFogDensity     = IL2CPP::Class::Utils::GetMethodPointer("UnityEngine.RenderSettings", "set_fogDensity", 1);
    g_lightIntensity    = GetFieldOffset("UnityEngine.Light", "intensity");
    LOGD("FullBright: mode=%p sky=%p eq=%p fogCol=%p fogDen=%p lightInt=%d",
         g_setAmbientMode, g_setAmbientSky, g_setAmbientEquator, g_setFogColor, g_setFogDensity, g_lightIntensity);
    g_fpGrounded         = GetFieldOffset("FP_Controller", "grounded");    LogOffset("FP_Controller","grounded",g_fpGrounded);
    g_fpCanJump          = GetFieldOffset("FP_Controller", "canJump");     LogOffset("FP_Controller","canJump",g_fpCanJump);
    g_fpMoveDir          = GetFieldOffset("FP_Controller", "moveDirection"); LogOffset("FP_Controller","moveDirection",g_fpMoveDir);
    g_fpJumpTimer        = GetFieldOffset("FP_Controller", "jumpTimer");   LogOffset("FP_Controller","jumpTimer",g_fpJumpTimer);
    g_fpAntiBunnyHop     = GetFieldOffset("FP_Controller", "antiBunnyHopFactor"); LogOffset("FP_Controller","antiBunnyHopFactor",g_fpAntiBunnyHop);
    LoadConfig();
    LOGD("OFFSETS: Stam(%d,%d,%d) Health(%d,%d,%d) Bat(%d,%d,%d) Elec(%d,%d,%d) FP(%d,%d,%d,%d,%d,%d,%d,%d) Term(%d,%d) quota(%d,%d,%d) Item(%d) Tele(%d,%d) Ship(%d) Ads(%d,%d,%d,%d) Door(%d,%d,%d) En(%d,%d,%d)",
        g_staminaWasteSpeed, g_staminaGainSpeed, g_staminaCanRun,
        g_healthPoints, g_healthIsDead, g_healthUndamage,
        g_flashWasteSpeed, g_flashDischarge, g_flashBatteryField,
        g_elecCapacity, g_elecDischargeSpeed, g_elecIsDischarged,
        g_fpWalkSpeed, g_fpRunSpeed, g_fpJumpForce, g_fpGravity, g_fpAirControl, g_fpFallDamageVel, g_fpGrounded, g_fpCanJump, g_fpMoveDir, g_fpJumpTimer, g_fpAntiBunnyHop,
        g_shopMoney, g_shopTotalPrice,
        g_quotaDeadline, g_quotaAmount, g_quotaTotalCollected,
        g_itemValue, g_teleTimeout, g_teleTimePassed, g_shipDoorPower,
        g_adsShowedAd, g_adsOpenOnlineMode, g_cfgOnlineModeActive, g_configShowAdsDuring, g_configShowAdsBefore,
        g_doorSeconds, g_doorDelayBuffer, g_doorStopDelayCor,
        g_enemyLocalPlayer, g_enemyNavMesh, g_enemyActivePlayers);
    g_resolved = true;
    LOGD("offsets resolved + config loaded");
}

void SetupEconomyHooks() {
    if (!g_moneyHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("TerminalShop", "onMoneyAmountChanged", 2);
        if (ptr) {
            DobbyHook(ptr, (dobby_dummy_func_t)my_onMoneyAmountChanged, (dobby_dummy_func_t*)&orig_onMoneyAmountChanged);
            g_moneyHookSetup = true;
            LOGD("Hooked onMoneyAmountChanged");
        }
    }
    if (!g_totalPriceHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("TerminalShop", "CalculateTotalPrice", 0);
        if (ptr) {
            DobbyHook(ptr, (dobby_dummy_func_t)my_CalculateTotalPrice, (dobby_dummy_func_t*)&orig_CalculateTotalPrice);
            g_totalPriceHookSetup = true;
            LOGD("Hooked CalculateTotalPrice");
        }
    }
    if (!g_elecCapHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("ElectricItem", "set_Networkcapacity", 1);
        if (ptr) {
            DobbyHook(ptr, (dobby_dummy_func_t)my_set_Networkcapacity, (dobby_dummy_func_t*)&orig_set_Networkcapacity);
            g_elecCapHookSetup = true;
            LOGD("Hooked set_Networkcapacity");
        }
    }
    if (!g_elecDiscHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("ElectricItem", "set_NetworkisDiscarged", 1);
        if (ptr) {
            DobbyHook(ptr, (dobby_dummy_func_t)my_set_NetworkisDiscarged, (dobby_dummy_func_t*)&orig_set_NetworkisDiscarged);
            g_elecDiscHookSetup = true;
            LOGD("Hooked set_NetworkisDiscarged");
        }
    }
    if (!g_teleHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("TeleporterButton", "set_NetworktimePassed", 1);
        if (ptr) {
            DobbyHook(ptr, (dobby_dummy_func_t)my_set_NetworktimePassed, (dobby_dummy_func_t*)&orig_set_NetworktimePassed);
            g_teleHookSetup = true;
            LOGD("Hooked set_NetworktimePassed");
        }
    }
    if (!g_scrapHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("InventoryItem", "set_NetworkisActive", 1);
        if (ptr) {
            DobbyHook(ptr, (dobby_dummy_func_t)my_set_NetworkisActive, (dobby_dummy_func_t*)&orig_set_NetworkisActive);
            g_scrapHookSetup = true;
            LOGD("Hooked set_NetworkisActive");
        }
    }
    if (!g_quotaHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("quota", "updateView", 2);
        if (ptr) {
            DobbyHook(ptr, (dobby_dummy_func_t)my_quota_updateView, (dobby_dummy_func_t*)&orig_quota_updateView);
            g_quotaHookSetup = true;
            LOGD("Hooked quota::updateView");
        }
    }
    if (!g_antiKickHookSetup) {
        void* ptr = IL2CPP::Class::Utils::GetMethodPointer("ClientsInLobby", "AddPlayerToKickList", 1);
        if (ptr) {
            DobbyHook(ptr, (dobby_dummy_func_t)my_AddPlayerToKickList, (dobby_dummy_func_t*)&orig_AddPlayerToKickList);
            g_antiKickHookSetup = true;
            LOGD("Hooked ClientsInLobby::AddPlayerToKickList");
        }
    }
    if (!g_antiBanHookSetup) {
        bool ok = false;
        void* p1 = IL2CPP::Class::Utils::GetMethodPointer("ServerMsgHandler", "OnMessageRecive", 1);
        if (p1) {
            DobbyHook(p1, (dobby_dummy_func_t)my_OnMessageRecive, (dobby_dummy_func_t*)&orig_OnMessageRecive);
            LOGD("Hooked ServerMsgHandler::OnMessageRecive");
            ok = true;
        }
        void* p2 = IL2CPP::Class::Utils::GetMethodPointer("ServerMsgHandler", "showDisconnectPopUp", 1);
        if (p2) {
            DobbyHook(p2, (dobby_dummy_func_t)my_showDisconnectPopUp, (dobby_dummy_func_t*)&orig_showDisconnectPopUp);
            LOGD("Hooked ServerMsgHandler::showDisconnectPopUp");
            ok = true;
        }
        void* p3 = IL2CPP::Class::Utils::GetMethodPointer("NetworkChat", "TargetShowDisconnectMessage", 2);
        if (p3) {
            DobbyHook(p3, (dobby_dummy_func_t)my_TargetShowDisconnectMessage, (dobby_dummy_func_t*)&orig_TargetShowDisconnectMessage);
            LOGD("Hooked NetworkChat::TargetShowDisconnectMessage");
            ok = true;
        }
        if (ok) g_antiBanHookSetup = true;
    }

}

void ApplyCheats() {
    if (!IL2CPP::Globals.m_GameAssembly) return;
    if (!g_resolved) return;

    // Ensure economy hooks are installed (guarded by flags — one-shot).
    SetupEconomyHooks();

    static int g_slot = 0;
    static int g_findThrottle = 0;

    {
        static bool g_prevCamHadTransform = false;
        bool camHasTransform = false;
        Unity::CCamera* cam = Unity::Camera::GetMain();
        if (cam && reinterpret_cast<Unity::CComponent*>(cam)->GetTransform())
            camHasTransform = true;

        if (g_prevCamHadTransform && !camHasTransform) {
            LOGD("transition START");
            g_inTransition = true;
            g_transitionCooldown = 0;
        }
        if (!g_prevCamHadTransform && camHasTransform) {
            LOGD("transition END");
            g_transitionCooldown = 5;
            g_r = {};
        }
        g_prevCamHadTransform = camHasTransform;
    }

    if (g_transitionCooldown > 0) {
        g_transitionCooldown--;
        if (g_transitionCooldown == 0) {
            g_inTransition = false;
            ResolveOffsets();
            LOGD("transition done");
        }
    }

    if (g_inTransition) return;

    g_slot = (g_slot + 1) % 5;

    // Slot 0: Stamina
    if (g_slot == 0) {
        bool anyStamina = g_config.infiniteStamina || g_config.unlimitedSprint || g_config.staminaManipulation;
        bool prevAnyStamina = g_r.prevInfiniteStamina || g_r.prevUnlimitedSprint || g_r.prevStaminaManipulation;

        if (anyStamina || prevAnyStamina) {
            auto* arr = Unity::Object::FindObjectsOfType<void>("Stamina");
            void* st = (arr && arr->m_uMaxLength > 0) ? arr->At(0) : nullptr;

            if (!anyStamina && prevAnyStamina && st) {
                if (g_staminaWasteSpeed >= 0 && std::isfinite(g_r.origStaminaWasteSpeed))
                    SetFloatIfChanged(st, g_staminaWasteSpeed, g_r.origStaminaWasteSpeed);
                if (g_staminaGainSpeed >= 0 && std::isfinite(g_r.origStaminaGainSpeed))
                    SetFloatIfChanged(st, g_staminaGainSpeed, g_r.origStaminaGainSpeed);
                if (g_staminaCanRun >= 0 && g_r.prevUnlimitedSprint)
                    SetU8IfChanged(st, g_staminaCanRun, g_r.origCanRun);
            } else if (anyStamina && st) {
                bool justEnabled = !prevAnyStamina;
                if (justEnabled) {
                    if (g_staminaWasteSpeed >= 0) g_r.origStaminaWasteSpeed = AsClass(st)->GetMemberValue<float>(g_staminaWasteSpeed);
                    if (g_staminaGainSpeed  >= 0) g_r.origStaminaGainSpeed  = AsClass(st)->GetMemberValue<float>(g_staminaGainSpeed);
                    if (g_staminaCanRun     >= 0) g_r.origCanRun = AsClass(st)->GetMemberValue<uint8_t>(g_staminaCanRun);
                }
                if (g_config.infiniteStamina || g_config.staminaManipulation) {
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

    // Slot 1: Movement
    if (g_slot == 1) {
        bool anyMove = g_config.speedHack || g_config.superJump || g_config.noGravity || g_config.airControl || g_config.noFallDamage;
        bool prevAnyMove = g_r.prevSpeedHack || g_r.prevSuperJump || g_r.prevNoGravity || g_r.prevAirControl || g_r.prevNoFallDamage;

        if (anyMove || prevAnyMove) {
            auto* fpArr = Unity::Object::FindObjectsOfType<void>("FP_Controller");
            void* fp = (fpArr && fpArr->m_uMaxLength > 0) ? fpArr->At(0) : nullptr;

            if (!anyMove && prevAnyMove && fp) {
                if (g_fpWalkSpeed     >= 0 && std::isfinite(g_r.origWalkSpeed))    SetFloatIfChanged(fp, g_fpWalkSpeed, g_r.origWalkSpeed);
                if (g_fpRunSpeed      >= 0 && std::isfinite(g_r.origRunSpeed))     SetFloatIfChanged(fp, g_fpRunSpeed, g_r.origRunSpeed);
                if (g_fpJumpForce     >= 0 && std::isfinite(g_r.origJumpForce))    SetFloatIfChanged(fp, g_fpJumpForce, g_r.origJumpForce);
                if (g_fpGravity       >= 0 && std::isfinite(g_r.origGravity))      SetFloatIfChanged(fp, g_fpGravity, g_r.origGravity);
                if (g_fpAirControl    >= 0)                                          SetBoolIfChanged(fp, g_fpAirControl, g_r.origAirControl);
                if (g_fpFallDamageVel >= 0 && std::isfinite(g_r.origFallDamageVel)) SetFloatIfChanged(fp, g_fpFallDamageVel, g_r.origFallDamageVel);
            } else if (!prevAnyMove && anyMove && fp) {
                if (g_fpWalkSpeed     >= 0) g_r.origWalkSpeed     = AsClass(fp)->GetMemberValue<float>(g_fpWalkSpeed);
                if (g_fpRunSpeed      >= 0) g_r.origRunSpeed      = AsClass(fp)->GetMemberValue<float>(g_fpRunSpeed);
                if (g_fpJumpForce     >= 0) g_r.origJumpForce     = AsClass(fp)->GetMemberValue<float>(g_fpJumpForce);
                if (g_fpGravity       >= 0) g_r.origGravity       = AsClass(fp)->GetMemberValue<float>(g_fpGravity);
                if (g_fpAirControl    >= 0) g_r.origAirControl    = AsClass(fp)->GetMemberValue<bool>(g_fpAirControl);
                if (g_fpFallDamageVel >= 0) g_r.origFallDamageVel = AsClass(fp)->GetMemberValue<float>(g_fpFallDamageVel);
            }

            if (fp) {
                if (g_config.speedHack) {
                    if (g_fpWalkSpeed >= 0) SetFloatIfChanged(fp, g_fpWalkSpeed, 8.0f);
                    if (g_fpRunSpeed  >= 0) SetFloatIfChanged(fp, g_fpRunSpeed,  12.0f);
                }
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

    // Slot 2: God Mode
    if (g_slot == 2) {
        if (g_config.godMode || g_r.prevGodMode) {
            auto* arr = Unity::Object::FindObjectsOfType<void>("PlayerHealth");
            if (!arr || arr->m_uMaxLength == 0)
                arr = Unity::Object::FindObjectsOfType<void>("Health");
            void* ph = (arr && arr->m_uMaxLength > 0) ? arr->At(0) : nullptr;

            if (!g_config.godMode && g_r.prevGodMode && ph) {
                if (g_healthPoints   >= 0 && std::isfinite(g_r.origHealthPoints)) SetFloatIfChanged(ph, g_healthPoints, g_r.origHealthPoints);
                if (g_healthIsDead   >= 0)                                         SetU8IfChanged(ph, g_healthIsDead, g_r.origIsDead);
                if (g_healthUndamage >= 0)                                         SetBoolIfChanged(ph, g_healthUndamage, g_r.origUndamageable);
            } else if (g_config.godMode && ph) {
                if (!g_r.prevGodMode) {
                    if (g_healthPoints   >= 0) g_r.origHealthPoints  = AsClass(ph)->GetMemberValue<float>(g_healthPoints);
                    if (g_healthIsDead   >= 0) g_r.origIsDead        = AsClass(ph)->GetMemberValue<uint8_t>(g_healthIsDead);
                    if (g_healthUndamage >= 0) g_r.origUndamageable  = AsClass(ph)->GetMemberValue<bool>(g_healthUndamage);
                }
                if (g_healthPoints   >= 0) SetFloatIfChanged(ph, g_healthPoints, 200.0f);
                if (g_healthIsDead   >= 0) SetU8IfChanged(ph, g_healthIsDead, 0);
                if (g_healthUndamage >= 0) SetBoolIfChanged(ph, g_healthUndamage, true);
            }
        }
        g_r.prevGodMode = g_config.godMode;
    }

    // Slot 3: Items
    if (g_slot == 3) {
        bool anyActive = g_config.unlimitedFlashlight || g_config.unlimitedUsage ||
            g_config.infiniteJetpackFuel || g_config.noWeightLimit ||
            g_config.superItemMagnet || g_config.autoPickup ||
            g_config.setScrapValue;
        bool anyPrev = g_r.prevUnlimitedFlashlight || g_r.prevUnlimitedUsage ||
            g_r.prevInfiniteJetpackFuel || g_r.prevNoWeightLimit ||
            g_r.prevSuperItemMagnet || g_r.prevAutoPickup ||
            g_r.prevSetScrapValue;

        if (anyActive || anyPrev) {

    if (g_config.unlimitedFlashlight || g_r.prevUnlimitedFlashlight) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("BatterySlider");
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

    if (g_config.unlimitedUsage || g_config.infiniteJetpackFuel ||
        g_r.prevUnlimitedUsage || g_r.prevInfiniteJetpackFuel)
    {
        bool anyElec = g_config.unlimitedUsage || g_config.infiniteJetpackFuel;
        auto* arr = Unity::Object::FindObjectsOfType<void>("ElectricItem");

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

    if (g_config.noWeightLimit || g_r.prevNoWeightLimit) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("Inventory");
        void* inv = (arr && arr->m_uMaxLength > 0) ? arr->At(0) : nullptr;
        if (!g_config.noWeightLimit && g_r.prevNoWeightLimit && inv && g_invMaxWeight >= 0 && std::isfinite(g_r.origInvMaxWeight))
            SetFloatIfChanged(inv, g_invMaxWeight, g_r.origInvMaxWeight);
        else if (g_config.noWeightLimit && inv && g_invMaxWeight >= 0) {
            if (!g_r.prevNoWeightLimit)
                g_r.origInvMaxWeight = AsClass(inv)->GetMemberValue<float>(g_invMaxWeight);
            SetFloatIfChanged(inv, g_invMaxWeight, 500.0f);
        }
        g_r.prevNoWeightLimit = g_config.noWeightLimit;
    }

    if (g_config.superItemMagnet || g_config.autoPickup ||
        g_r.prevSuperItemMagnet || g_r.prevAutoPickup)
    {
        bool anyMagnet = g_config.superItemMagnet || g_config.autoPickup;
        auto* arr = Unity::Object::FindObjectsOfType<void>("ItemMagnet");

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
                if (!(g_r.prevSuperItemMagnet || g_r.prevAutoPickup) && i == 0) {
                    if (g_magnetStrength >= 0) g_r.origMagnetStrength = AsClass(mag)->GetMemberValue<float>(g_magnetStrength);
                    if (g_magnetMaxItems >= 0) g_r.origMagnetMaxItems = AsClass(mag)->GetMemberValue<int>(g_magnetMaxItems);
                }
                float strength = g_config.autoPickup ? 50.0f : 35.0f;
                if (g_magnetStrength >= 0) SetFloatIfChanged(mag, g_magnetStrength, strength);
                if (g_config.autoPickup && g_magnetMaxItems >= 0)
                    SetIntIfChanged(mag, g_magnetMaxItems, 10);
            }
        }
        g_r.prevSuperItemMagnet = g_config.superItemMagnet;
        g_r.prevAutoPickup     = g_config.autoPickup;
    }

    if (g_config.setScrapValue || g_r.prevSetScrapValue) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("InventoryItem");

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
                if (!g_r.prevSetScrapValue && i == 0 && g_itemValue >= 0)
                    g_r.origItemValue = AsClass(item)->GetMemberValue<int>(g_itemValue);
                if (g_itemValue >= 0) SetIntIfChanged(item, g_itemValue, 999);
            }
        }
        g_r.prevSetScrapValue = g_config.setScrapValue;
    }

        }
    }

    // Slot 4: Combat
    if (g_slot == 4) {
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
    }

    // Throttled block
    bool canFind = (++g_findThrottle >= 3);
    if (canFind) g_findThrottle = 0;

    if (canFind) {

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

    if (g_config.infinityJumps || g_r.prevInfinityJumps) {
        auto* fpArr = Unity::Object::FindObjectsOfType<void>("FP_Controller");
        void* fp = (fpArr && fpArr->m_uMaxLength > 0) ? fpArr->At(0) : nullptr;
        if (fp) {
            if (!g_config.infinityJumps && g_r.prevInfinityJumps) {
                if (g_fpCanJump >= 0)
                    SetBoolIfChanged(fp, g_fpCanJump, g_r.origCanJump);
            } else if (g_config.infinityJumps) {
                if (!g_r.prevInfinityJumps && g_fpCanJump >= 0)
                    g_r.origCanJump = AsClass(fp)->GetMemberValue<bool>(g_fpCanJump);
                if (g_fpCanJump >= 0)
                    SetBoolIfChanged(fp, g_fpCanJump, true);
            }
        }
        g_r.prevInfinityJumps = g_config.infinityJumps;
    }

    if (g_config.instantDoors || g_r.prevInstantDoors) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("OpenDoorTrigger");
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
    }

    if (g_config.instantTeleport) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("TeleportToShip");
        void* tp = (arr && arr->m_uMaxLength > 0) ? arr->At(0) : nullptr;
        if (tp) {
            // TeleportTarget() is a [Command] that runs server-side and bails out if
            // selectedTarget is null (it's chosen locally by SelectTarget), so calling
            // the pair often does nothing. Invoke the private teleportPlayer() that
            // actually performs the move; fall back to the Select+Teleport pair.
            bool done = false;
            static void* g_teleportPlayerPtr = nullptr;
            if (!g_teleportPlayerPtr)
                g_teleportPlayerPtr = IL2CPP::Class::Utils::GetMethodPointer("TeleportToShip", "teleportPlayer", 0);
            if (g_teleportPlayerPtr) {
                InvokeManaged(tp, "TeleportToShip", "teleportPlayer", 0, nullptr);
                done = true;
                LOGD("Instant teleport: teleportPlayer()");
            } else {
                InvokeManaged(tp, "TeleportToShip", "SelectTarget", 0, nullptr);
                InvokeManaged(tp, "TeleportToShip", "TeleportTarget", 0, nullptr);
                done = true;
                LOGD("Instant teleport: SelectTarget+TeleportTarget");
            }
            if (done) g_config.instantTeleport = false;
        }
        // If tp isn't ready yet, leave the flag set so we retry next tick.
    }

    if (g_config.kickAllPlayers || g_config.forceSpectatorAll || g_config.clearKickList) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("ClientsInLobby");
        void* clients = (arr && arr->m_uMaxLength > 0) ? arr->At(0) : nullptr;
        if (clients) {
            void* myId = (g_clientsMyIdentity >= 0)
                ? AsClass(clients)->GetMemberValue<void*>(g_clientsMyIdentity) : nullptr;

            // Clear My Kick List: keep kickedPlayersIds empty so you can rejoin
            if (g_config.clearKickList && g_clientsKickedIds >= 0) {
                void* kickList = AsClass(clients)->GetMemberValue<void*>(g_clientsKickedIds);
                if (kickList && AsClass(kickList)->GetMemberValue<int>(0x18) > 0) {
                    AsClass(kickList)->SetMemberValue<int>(0x18, 0); // List<int>._size = 0
                    LOGD("ClearKickList: emptied kickedPlayersIds");
                }
            }

            // Native Kick All via each player's own kick button
            if (g_config.kickAllPlayers) {
                int kicked = 0;
                if (g_clientsControls >= 0 && g_controlIdentity >= 0) {
                    void* ctrlsObj = AsClass(clients)->GetMemberValue<void*>(g_clientsControls);
                    if (ctrlsObj) {
                        auto* ctrls = reinterpret_cast<Unity::il2cppArray<void*>*>(ctrlsObj);
                        for (uintptr_t i = 0; i < ctrls->m_uMaxLength; i++) {
                            void* c = ctrls->At(i);
                            if (!c) continue;
                            void* id = AsClass(c)->GetMemberValue<void*>(g_controlIdentity);
                            if (!id || id == myId) continue;
                            InvokeManaged(c, "ClientInfoControll", "OnKickButtonPressed", 0, nullptr);
                            kicked++;
                        }
                    }
                }
                // Fallback: kick via ClientsInLobby::AddPlayerToKickList using the Clients list
                if (kicked == 0 && g_clientsList >= 0 && g_clientInfoIdentity >= 0) {
                    void* listObj = AsClass(clients)->GetMemberValue<void*>(g_clientsList);
                    if (listObj) {
                        void* itemsObj = AsClass(listObj)->GetMemberValue<void*>(0x10);
                        int size = AsClass(listObj)->GetMemberValue<int>(0x18);
                        if (itemsObj && size > 0) {
                            auto* items = reinterpret_cast<Unity::il2cppArray<void*>*>(itemsObj);
                            for (int i = 0; i < size && (uintptr_t)i < items->m_uMaxLength; i++) {
                                void* ci = items->At(i);
                                if (!ci) continue;
                                void* id = AsClass(ci)->GetMemberValue<void*>(g_clientInfoIdentity);
                                if (!id || id == myId) continue;
                                void* a[1] = { id };
                                InvokeManaged(clients, "ClientsInLobby", "AddPlayerToKickList", 1, a);
                                kicked++;
                            }
                        }
                    }
                }
                LOGD("KickAll: kicked %d player(s)", kicked);
            }

            // Force Spectator on others via no-authority command
            if (g_config.forceSpectatorAll &&
                g_clientsList >= 0 && g_clientInfoIdentity >= 0 && g_clientsNetworkChat >= 0)
            {
                void* chat = AsClass(clients)->GetMemberValue<void*>(g_clientsNetworkChat);
                void* listObj = AsClass(clients)->GetMemberValue<void*>(g_clientsList);
                if (chat && listObj) {
                    void* itemsObj = AsClass(listObj)->GetMemberValue<void*>(0x10);
                    int size = AsClass(listObj)->GetMemberValue<int>(0x18);
                    if (itemsObj && size > 0) {
                        auto* items = reinterpret_cast<Unity::il2cppArray<void*>*>(itemsObj);
                        int done = 0;
                        for (int i = 0; i < size && (uintptr_t)i < items->m_uMaxLength; i++) {
                            void* ci = items->At(i);
                            if (!ci) continue;
                            void* id = AsClass(ci)->GetMemberValue<void*>(g_clientInfoIdentity);
                            if (!id || id == myId) continue;
                            bool status = true;
                            void* a[2] = { id, &status };
                            InvokeManaged(chat, "NetworkChat", "CmdSetClientSpectatorStatus", 2, a);
                            done++;
                        }
                        LOGD("ForceSpectator: applied to %d player(s)", done);
                    }
                }
            }
        }
        g_config.kickAllPlayers = false;
        g_config.forceSpectatorAll = false;
    }

    // ── Grief features (oneshot, server-authoritative: needs host / server trust) ──
    if (g_config.griefTeleportShip || g_config.griefKill || g_config.griefOpenDoors ||
        g_config.griefTraps || g_config.griefSpawnEnemies || g_config.griefBoombox ||
        g_config.griefShuffleItems) {

        // Local-player identity, used to avoid griefing ourselves.
        void* localId = nullptr;
        auto* cab = Unity::Object::FindObjectsOfType<void>("ClientsInLobby");
        void* clients = (cab && cab->m_uMaxLength > 0) ? cab->At(0) : nullptr;
        if (clients && g_clientsMyIdentity >= 0)
            localId = AsClass(clients)->GetMemberValue<void*>(g_clientsMyIdentity);

        auto IsSelf = [&](void* ci) -> bool {
            if (!localId || !ci) return false;
            if (ci == localId) return true;
            if (g_clientInfoNetId >= 0) {
                void* a = AsClass(ci)->GetMemberValue<void*>(g_clientInfoNetId);
                void* b = AsClass(localId)->GetMemberValue<void*>(g_clientInfoNetId);
                if (a && b && a == b) return true;
            }
            return false;
        };

        // Teleport every other player to the ship.
        if (g_config.griefTeleportShip) {
            auto* arr = Unity::Object::FindObjectsOfType<void>("TeleportToShip");
            if (arr) {
                int n = 0;
                for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                    void* tp = arr->At(i);
                    if (!tp) continue;
                    CRASH_GUARD({
                        InvokeManaged(tp, "TeleportToShip", "SelectTarget", 0, nullptr);
                        InvokeManaged(tp, "TeleportToShip", "TeleportTarget", 0, nullptr);
                    });
                    n++;
                }
                LOGD("Grief: teleport-to-ship x%d", n);
            }
        }

        // Kill every other player.
        if (g_config.griefKill) {
            auto* arr = Unity::Object::FindObjectsOfType<void>("PlayerHealth");
            if (arr) {
                int n = 0;
                for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                    void* ph = arr->At(i);
                    if (!ph || IsSelf(ph)) continue;
                    CRASH_GUARD(InvokeManaged(ph, "PlayerHealth", "Kill", 0, nullptr));
                    n++;
                }
                LOGD("Grief: killed %d player(s)", n);
            }
        }

        // Open every door + ship door.
        if (g_config.griefOpenDoors) {
            auto* doors = Unity::Object::FindObjectsOfType<void>("OpenDoorTrigger");
            if (doors) {
                int n = 0;
                for (uintptr_t i = 0; i < doors->m_uMaxLength; i++) {
                    void* d = doors->At(i);
                    if (!d) continue;
                    CRASH_GUARD(InvokeManaged(d, "OpenDoorTrigger", "Open", 0, nullptr));
                    n++;
                }
                LOGD("Grief: opened %d door(s)", n);
            }
            auto* ship = Unity::Object::FindObjectsOfType<void>("ShipDoorController");
            if (ship) {
                for (uintptr_t i = 0; i < ship->m_uMaxLength; i++) {
                    void* sd = ship->At(i);
                    if (!sd) continue;
                    if (g_shipDoorPower >= 0) SetIntIfChanged(sd, g_shipDoorPower, 100);
                    int sp = 100;
                    void* a[1] = { &sp };
                    CRASH_GUARD(InvokeManaged(sd, "ShipDoorController", "constantPowerLevelChange", 1, a));
                }
                LOGD("Grief: ship doors opened");
            }
        }

        // Detonate every mine under all players.
        if (g_config.griefTraps) {
            auto* mines = Unity::Object::FindObjectsOfType<void>("MineTrigger");
            if (mines) {
                int n = 0;
                for (uintptr_t i = 0; i < mines->m_uMaxLength; i++) {
                    void* m = mines->At(i);
                    if (!m) continue;
                    uint32_t ignore = 0;
                    void* a[1] = { &ignore };
                    CRASH_GUARD(InvokeManaged(m, "MineTrigger", "CmdExplode", 1, a, 1 /*uint by value*/));
                    n++;
                }
                LOGD("Grief: detonated %d mine(s)", n);
            }
        }

        // Force-enable every boombox (lures enemies).
        if (g_config.griefBoombox) {
            auto* boxes = Unity::Object::FindObjectsOfType<void>("BoomBox");
            if (boxes) {
                int n = 0;
                for (uintptr_t i = 0; i < boxes->m_uMaxLength; i++) {
                    void* b = boxes->At(i);
                    if (!b) continue;
                    int idx = 0;
                    void* a[1] = { &idx };
                    CRASH_GUARD(InvokeManaged(b, "BoomBox", "ChangeSong", 0, nullptr));
                    n++;
                }
                LOGD("Grief: boombox spam x%d", n);
            }
        }

        // Spawn enemies near players (best-effort — names vary by build).
        if (g_config.griefSpawnEnemies) {
            int n = 0;
            auto* worms = Unity::Object::FindObjectsOfType<void>("spawnSandWorm");
            if (worms) {
                for (uintptr_t i = 0; i < worms->m_uMaxLength; i++) {
                    void* w = worms->At(i);
                    if (!w) continue;
                    CRASH_GUARD(InvokeManaged(w, "spawnSandWorm", "SpawnAttack", 0, nullptr));
                    n++;
                }
            }
            auto* jesters = Unity::Object::FindObjectsOfType<void>("SpawnJester");
            if (jesters) {
                for (uintptr_t i = 0; i < jesters->m_uMaxLength; i++) {
                    void* j = jesters->At(i);
                    if (!j) continue;
                    CRASH_GUARD(InvokeManaged(j, "SpawnJester", "Spawn", 0, nullptr));
                    n++;
                }
            }
            LOGD("Grief: enemy spawners triggered x%d", n);
        }

        // Shuffle/drop every item to a random spot near the ship.
        if (g_config.griefShuffleItems) {
            auto* items = Unity::Object::FindObjectsOfType<void>("InventoryItem", true);
            if (!items) items = Unity::Object::FindObjectsOfType<void>("GrabbableObject", true);
            if (items) {
                int n = 0;
                for (uintptr_t i = 0; i < items->m_uMaxLength; i++) {
                    void* it = items->At(i);
                    if (!it) continue;
                    CRASH_GUARD({
                        void* tr = GetTransformOf(it);
                        if (tr) {
                            Unity::Vector3 p = GetPos(tr);
                            p.X += ((float)rand() / RAND_MAX - 0.5f) * 12.0f;
                            p.Y += ((float)rand() / RAND_MAX) * 4.0f;
                            p.Z += ((float)rand() / RAND_MAX - 0.5f) * 12.0f;
                            SetPos(tr, p);
                        }
                    });
                    n++;
                }
                LOGD("Grief: shuffled %d item(s)", n);
            }
        }

        g_config.griefTeleportShip = false;
        g_config.griefKill = false;
        g_config.griefOpenDoors = false;
        g_config.griefTraps = false;
        g_config.griefSpawnEnemies = false;
        g_config.griefBoombox = false;
        g_config.griefShuffleItems = false;
    }

    if (g_config.autoReward) {
        AutoReward_SwitchToOnline();
    }

    // Deferred one-shot button actions — executed here on the Unity main thread
    // (the button click itself arrives on the Android UI thread and would freeze
    //  the game if it touched scene/UnityEvent objects directly).
    if (g_requestBypassReward) {
        AutoReward_SwitchToOnline();
        g_requestBypassReward = false;
    }
    if (g_requestSwitchOnline) {
        AutoReward_SwitchToOnline();
        g_requestSwitchOnline = false;
    }

    if (g_config.blockAds) {
        auto* arr = Unity::Object::FindObjectsOfType<void>("ConfigDownLoader");
        if (arr) {
            for (uintptr_t i = 0; i < arr->m_uMaxLength; i++) {
                void* cfg = arr->At(i);
                if (!cfg) continue;
                if (g_configShowAdsDuring >= 0)
                    AsClass(cfg)->SetMemberValue<bool>(g_configShowAdsDuring, false);
                if (g_configShowAdsBefore >= 0)
                    AsClass(cfg)->SetMemberValue<bool>(g_configShowAdsBefore, false);
            }
        }
    }

    // Full bright — kill darkness from every source (ambient, fog, real lights)
    if (g_config.fullBright) {
        if (g_setAmbientMode) {
            int mode = 3; // AmbientMode.Flat
            void* a[1] = { &mode };
            InvokeManaged(nullptr, "UnityEngine.RenderSettings", "set_ambientMode", 1, a, 1 /*mode by value*/);
        }
        Unity::Color white(1.f, 1.f, 1.f, 1.f);
        if (g_setAmbientSky) {
            void* a[1] = { &white };
            InvokeManaged(nullptr, "UnityEngine.RenderSettings", "set_ambientSkyColor", 1, a);
        }
        if (g_setAmbientEquator) {
            void* a[1] = { &white };
            InvokeManaged(nullptr, "UnityEngine.RenderSettings", "set_ambientEquatorColor", 1, a);
        }
        // Fog is the usual culprit for dark interiors — force it to clear white.
        if (g_setFogColor) {
            void* a[1] = { &white };
            InvokeManaged(nullptr, "UnityEngine.RenderSettings", "set_fogColor", 1, a);
        }
        if (g_setFogDensity) {
            if (g_fbOrigFogDensity < 0.0f) {
                // Snapshot the game's real fog density once (before we zero it) so we
                // can restore it when FullBright is turned off.
                void* gp = IL2CPP::Class::Utils::GetMethodPointer("UnityEngine.RenderSettings", "get_fogDensity", 0);
                if (gp) {
                    typedef void* (*t_invoke)(void*, void*, void**, void**);
                    static t_invoke s_ri = nullptr;
                    if (!s_ri) s_ri = reinterpret_cast<t_invoke>(IL2CPP::GetRuntimeInvoke());
                    void* exc = nullptr;
                    void* ret = s_ri((Unity::il2cppMethodInfo*)gp, nullptr, nullptr, &exc);
                    if (!exc && ret) {
                        float f;
                        memcpy(&f, &ret, sizeof(float)); // AArch64: float returned in low 32 bits
                        g_fbOrigFogDensity = f;
                    } else {
                        g_fbOrigFogDensity = 0.0f;
                    }
                } else {
                    g_fbOrigFogDensity = 0.0f;
                }
            }
            float zero = 0.0f;
            void* a[1] = { &zero };
            InvokeManaged(nullptr, "UnityEngine.RenderSettings", "set_fogDensity", 1, a, 1 /*density by value*/);
        }
        // Boost every real light's intensity so rooms are genuinely lit.
        if (g_lightIntensity >= 0) {
            auto* lights = Unity::Object::FindObjectsOfType<void>("Light");
            if (lights) {
                for (uintptr_t i = 0; i < lights->m_uMaxLength; i++) {
                    void* lt = lights->At(i);
                    if (!lt) continue;
                    float cur = AsClass(lt)->GetMemberValue<float>(g_lightIntensity);
                    if (cur < 5.0f)
                        AsClass(lt)->SetMemberValue<float>(g_lightIntensity, 5.0f);
                }
            }
        }
        g_fbApplied = true;
    } else if (g_fbApplied) {
        if (g_setAmbientMode) {
            int mode = 0; // AmbientMode.Skybox — restore default lighting
            void* a[1] = { &mode };
            InvokeManaged(nullptr, "UnityEngine.RenderSettings", "set_ambientMode", 1, a, 1 /*mode by value*/);
        }
        if (g_setFogDensity && g_fbOrigFogDensity >= 0.0f) {
            void* a[1] = { &g_fbOrigFogDensity };
            InvokeManaged(nullptr, "UnityEngine.RenderSettings", "set_fogDensity", 1, a, 1 /*density by value*/);
        }
        g_fbApplied = false;
    }
    }

    // Per-frame infinity jumps: keep grounded=true while not rising so the
    // jump can actually launch, and bypass the anti-bunny-hop timer so you can
    // chain jumps. Forcing grounded every tick is what stuck the player.
    if (g_config.infinityJumps) {
        auto* fpArr = Unity::Object::FindObjectsOfType<void>("FP_Controller");
        void* fp = (fpArr && fpArr->m_uMaxLength > 0) ? fpArr->At(0) : nullptr;
        if (fp && g_fpGrounded >= 0 && g_fpMoveDir >= 0) {
            Unity::Vector3 md = AsClass(fp)->GetMemberValue<Unity::Vector3>(g_fpMoveDir);
            if (md.Y <= 0.05f) {
                // Resting / falling: treat as grounded so the next jump is allowed
                AsClass(fp)->SetMemberValue<bool>(g_fpGrounded, true);
            } else {
                // Actively rising: do NOT clamp grounded, or the jump flattens out
                AsClass(fp)->SetMemberValue<bool>(g_fpGrounded, false);
            }
            if (g_fpJumpTimer >= 0 && g_fpAntiBunnyHop >= 0) {
                int thresh = AsClass(fp)->GetMemberValue<int>(g_fpAntiBunnyHop);
                int t = AsClass(fp)->GetMemberValue<int>(g_fpJumpTimer);
                if (thresh > 0 && t > 0)
                    AsClass(fp)->SetMemberValue<int>(g_fpJumpTimer, 0);
            }
        }
    }

    // Per-frame economy writes
    if (g_config.unlimitedMoney || g_r.prevUnlimitedMoney) {
        auto* shopArr = Unity::Object::FindObjectsOfType<void>("TerminalShop");
        void* shop = (shopArr && shopArr->m_uMaxLength > 0) ? shopArr->At(0) : nullptr;
        static int moneyDiag = 0;
        if (++moneyDiag <= 3 || moneyDiag % 1000 == 0)
            LOGI("MONEY: shop=%p cfg=%d prev=%d off=%d diag=%d", shop, (int)g_config.unlimitedMoney, (int)g_r.prevUnlimitedMoney, g_shopMoney, moneyDiag);
        if (shop && g_shopMoney >= 0) {
            if (g_config.unlimitedMoney) {
                // Money is a SyncVar: the server keeps pushing its own value back.
                // Bypass it by repeatedly sending CmdAddMoney (requiresAuthority=false,
                // so it executes locally on the host) and topping the local field up
                // only when it drops below target.
                int cur = AsClass(shop)->GetMemberValue<int>(g_shopMoney);
                if (cur < MONEY_TARGET) {
                    int addAmount = MONEY_TARGET - cur + 1000;
                    void* args[1] = { &addAmount };
                    InvokeManaged(shop, "TerminalShop", "CmdAddMoney", 1, args, 1 /*amount by value*/);
                    AsClass(shop)->SetMemberValue<int>(g_shopMoney, MONEY_TARGET);
                }
            }
        }
        g_r.prevUnlimitedMoney = g_config.unlimitedMoney;
    }

    if (g_config.freeItems || g_r.prevFreeItems) {
        auto* shopArr = Unity::Object::FindObjectsOfType<void>("TerminalShop");
        void* shop = (shopArr && shopArr->m_uMaxLength > 0) ? shopArr->At(0) : nullptr;
        if (shop && g_shopTotalPrice >= 0) {
            if (g_config.freeItems) {
                int cur = AsClass(shop)->GetMemberValue<int>(g_shopTotalPrice);
                if (cur != 0)
                    AsClass(shop)->SetMemberValue<int>(g_shopTotalPrice, 0);
            }
        }
        g_r.prevFreeItems = g_config.freeItems;
    }
}
