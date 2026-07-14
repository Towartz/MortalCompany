#pragma once
#include <cstdint>

struct Config {
    bool infiniteStamina = false;
    bool unlimitedSprint = false;
    bool speedHack = false;
    bool superJump = false;
    bool infinityJumps = false;
    bool noGravity = false;
    bool airControl = false;
    bool noFallDamage = false;
    bool godMode = false;
    bool staminaManipulation = false;
    bool unlimitedFlashlight = false;
    bool unlimitedUsage = false;
    bool infiniteJetpackFuel = false;
    bool noWeightLimit = false;
    bool superItemMagnet = false;
    bool autoPickup = false;
    bool setScrapValue = false;
    bool autoReward = false;
    bool oneHitKill = false;
    bool instantKillAll = false;
    bool blindEnemies = false;
    bool unlimitedMoney = false;
    bool freeItems = false;
    bool quotaManipulation = false;
    bool instantDoors = false;
    bool disableMines = false;
    bool shipDoorAlwaysOpen = false;
    bool noTeleporterCooldown = false;
    bool instantTeleport = false;
    bool blockAds = false;
    bool antiKick = false;
    bool kickAllPlayers = false;
    bool antiBan = false;
    bool clearKickList = false;
    bool forceSpectatorAll = false;
    bool griefTeleportShip = false;
    bool griefKill = false;
    bool griefOpenDoors = false;
    bool griefTraps = false;
    bool griefSpawnEnemies = false;
    bool griefBoombox = false;
    bool griefShuffleItems = false;
    bool espPlayers = false;
    bool espObjects = false;
    bool espBoxes = true;
    bool espTracelines = true;
    bool espLabels = true;
    bool espValuablesOnly = false;  // hide low-credit junk
    bool espShowNames = false;      // draw itemName under the marker
    float espMaxDist = 30.0f;
    bool fullBright = false;
};

extern Config g_config;
extern const char* g_configPath;

void LoadConfig();
void SaveConfig();
