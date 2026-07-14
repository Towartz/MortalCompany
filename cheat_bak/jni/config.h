#pragma once
#include <cstdint>

struct Config {
    // Movement
    bool infiniteStamina = false;
    bool unlimitedSprint = false;
    bool speedHack = false;
    bool superJump = false;
    bool infinityJumps = false;
    bool noGravity = false;
    bool airControl = false;
    bool noFallDamage = false;

    // Player
    bool godMode = false;
    bool staminaManipulation = false;

    // Items
    bool unlimitedFlashlight = false;
    bool unlimitedUsage = false;
    bool infiniteJetpackFuel = false;
    bool noWeightLimit = false;
    bool superItemMagnet = false;
    bool autoPickup = false;
    bool setScrapValue = false;
    bool autoReward = false;

    // Combat
    bool oneHitKill = false;
    bool instantKillAll = false;
    bool blindEnemies = false;

    // Economy
    bool unlimitedMoney = false;
    bool freeItems = false;
    bool quotaManipulation = false;

    // Traps & Doors
    bool instantDoors = false;
    bool disableMines = false;
    bool shipDoorAlwaysOpen = false;
    bool noTeleporterCooldown = false;

    // ESP
    bool espPlayers = false;
    bool espObjects = false;
    bool espBoxes = true;       // Player boxes (corner box, health bar, head dot)
    bool espTracelines = true;  // Player tracelines from bottom-center
    bool espLabels = true;      // All text labels (HP/distance for players, value for items)
    float espMaxDist = 30.0f;
};

extern Config g_config;
extern const char* g_configPath;

void LoadConfig();
void SaveConfig();
void CheckConfigReload(); // Auto-reload when file changes on disk
