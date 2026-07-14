#include "config.h"
#include <cstdio>
#include <cstring>
#include <android/log.h>
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "FBK", __VA_ARGS__)

Config g_config;
const char* g_configPath = "/sdcard/cheat.cfg";

static int getBool(const char* line, const char* key) {
    const char* p = strstr(line, key);
    if (!p) return -1;
    p = strchr(p, '=');
    if (!p) return -1;
    return (p[1] == '1') ? 1 : 0;
}

void LoadConfig() {
    FILE* f = fopen(g_configPath, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int v;
        if ((v = getBool(line, "infinite_stamina")) >= 0) g_config.infiniteStamina = v;
        else if ((v = getBool(line, "unlimited_sprint")) >= 0) g_config.unlimitedSprint = v;
        else if ((v = getBool(line, "speed_hack")) >= 0) g_config.speedHack = v;
        else if ((v = getBool(line, "super_jump")) >= 0) g_config.superJump = v;
        else if ((v = getBool(line, "infinity_jumps")) >= 0) g_config.infinityJumps = v;
        else if ((v = getBool(line, "no_gravity")) >= 0) g_config.noGravity = v;
        else if ((v = getBool(line, "air_control")) >= 0) g_config.airControl = v;
        else if ((v = getBool(line, "no_fall_damage")) >= 0) g_config.noFallDamage = v;
        else if ((v = getBool(line, "god_mode")) >= 0) g_config.godMode = v;
        else if ((v = getBool(line, "stamina_manipulation")) >= 0) g_config.staminaManipulation = v;
        else if ((v = getBool(line, "unlimited_flashlight")) >= 0) g_config.unlimitedFlashlight = v;
        else if ((v = getBool(line, "unlimited_usage")) >= 0) g_config.unlimitedUsage = v;
        else if ((v = getBool(line, "infinite_jetpack_fuel")) >= 0) g_config.infiniteJetpackFuel = v;
        else if ((v = getBool(line, "no_weight_limit")) >= 0) g_config.noWeightLimit = v;
        else if ((v = getBool(line, "super_item_magnet")) >= 0) g_config.superItemMagnet = v;
        else if ((v = getBool(line, "auto_pickup")) >= 0) g_config.autoPickup = v;
        else if ((v = getBool(line, "set_scrap_value")) >= 0) g_config.setScrapValue = v;
        else if ((v = getBool(line, "auto_reward")) >= 0) g_config.autoReward = v;
        else if ((v = getBool(line, "one_hit_kill")) >= 0) g_config.oneHitKill = v;
        else if ((v = getBool(line, "instant_kill_all")) >= 0) g_config.instantKillAll = v;
        else if ((v = getBool(line, "blind_enemies")) >= 0) g_config.blindEnemies = v;
        else if ((v = getBool(line, "unlimited_money")) >= 0) g_config.unlimitedMoney = v;
        else if ((v = getBool(line, "free_items")) >= 0) g_config.freeItems = v;
        else if ((v = getBool(line, "quota_manipulation")) >= 0) g_config.quotaManipulation = v;
        else if ((v = getBool(line, "instant_doors")) >= 0) g_config.instantDoors = v;
        else if ((v = getBool(line, "disable_mines")) >= 0) g_config.disableMines = v;
        else if ((v = getBool(line, "ship_door_always_open")) >= 0) g_config.shipDoorAlwaysOpen = v;
        else if ((v = getBool(line, "no_teleporter_cooldown")) >= 0) g_config.noTeleporterCooldown = v;
        else if ((v = getBool(line, "instant_teleport")) >= 0) g_config.instantTeleport = v;
        else if ((v = getBool(line, "block_ads")) >= 0) g_config.blockAds = v;
        else if ((v = getBool(line, "anti_kick")) >= 0) g_config.antiKick = v;
        else if ((v = getBool(line, "anti_ban")) >= 0) g_config.antiBan = v;
        else if ((v = getBool(line, "clear_kick_list")) >= 0) g_config.clearKickList = v;
        else if ((v = getBool(line, "grief_teleport_ship")) >= 0) g_config.griefTeleportShip = v;
        else if ((v = getBool(line, "grief_kill")) >= 0) g_config.griefKill = v;
        else if ((v = getBool(line, "grief_open_doors")) >= 0) g_config.griefOpenDoors = v;
        else if ((v = getBool(line, "grief_traps")) >= 0) g_config.griefTraps = v;
        else if ((v = getBool(line, "grief_spawn_enemies")) >= 0) g_config.griefSpawnEnemies = v;
        else if ((v = getBool(line, "grief_boombox")) >= 0) g_config.griefBoombox = v;
        else if ((v = getBool(line, "grief_shuffle_items")) >= 0) g_config.griefShuffleItems = v;
        else if ((v = getBool(line, "esp_players")) >= 0) g_config.espPlayers = v;
        else if ((v = getBool(line, "esp_objects")) >= 0) g_config.espObjects = v;
        else if ((v = getBool(line, "esp_boxes")) >= 0) g_config.espBoxes = v;
        else if ((v = getBool(line, "esp_tracelines")) >= 0) g_config.espTracelines = v;
        else if ((v = getBool(line, "esp_labels")) >= 0) g_config.espLabels = v;
        else if ((v = getBool(line, "esp_val_only")) >= 0) g_config.espValuablesOnly = v;
        else if ((v = getBool(line, "esp_names")) >= 0) g_config.espShowNames = v;
    }
    fclose(f);
}

void SaveConfig() {
    FILE* f = fopen(g_configPath, "w");
    if (!f) return;
    fprintf(f, "infinite_stamina=%d\n", g_config.infiniteStamina ? 1 : 0);
    fprintf(f, "unlimited_sprint=%d\n", g_config.unlimitedSprint ? 1 : 0);
    fprintf(f, "speed_hack=%d\n", g_config.speedHack ? 1 : 0);
    fprintf(f, "super_jump=%d\n", g_config.superJump ? 1 : 0);
    fprintf(f, "infinity_jumps=%d\n", g_config.infinityJumps ? 1 : 0);
    fprintf(f, "no_gravity=%d\n", g_config.noGravity ? 1 : 0);
    fprintf(f, "air_control=%d\n", g_config.airControl ? 1 : 0);
    fprintf(f, "no_fall_damage=%d\n", g_config.noFallDamage ? 1 : 0);
    fprintf(f, "god_mode=%d\n", g_config.godMode ? 1 : 0);
    fprintf(f, "stamina_manipulation=%d\n", g_config.staminaManipulation ? 1 : 0);
    fprintf(f, "unlimited_flashlight=%d\n", g_config.unlimitedFlashlight ? 1 : 0);
    fprintf(f, "unlimited_usage=%d\n", g_config.unlimitedUsage ? 1 : 0);
    fprintf(f, "infinite_jetpack_fuel=%d\n", g_config.infiniteJetpackFuel ? 1 : 0);
    fprintf(f, "no_weight_limit=%d\n", g_config.noWeightLimit ? 1 : 0);
    fprintf(f, "super_item_magnet=%d\n", g_config.superItemMagnet ? 1 : 0);
    fprintf(f, "auto_pickup=%d\n", g_config.autoPickup ? 1 : 0);
    fprintf(f, "set_scrap_value=%d\n", g_config.setScrapValue ? 1 : 0);
    fprintf(f, "auto_reward=%d\n", g_config.autoReward ? 1 : 0);
    fprintf(f, "one_hit_kill=%d\n", g_config.oneHitKill ? 1 : 0);
    fprintf(f, "instant_kill_all=%d\n", g_config.instantKillAll ? 1 : 0);
    fprintf(f, "blind_enemies=%d\n", g_config.blindEnemies ? 1 : 0);
    fprintf(f, "unlimited_money=%d\n", g_config.unlimitedMoney ? 1 : 0);
    fprintf(f, "free_items=%d\n", g_config.freeItems ? 1 : 0);
    fprintf(f, "quota_manipulation=%d\n", g_config.quotaManipulation ? 1 : 0);
    fprintf(f, "instant_doors=%d\n", g_config.instantDoors ? 1 : 0);
    fprintf(f, "disable_mines=%d\n", g_config.disableMines ? 1 : 0);
    fprintf(f, "ship_door_always_open=%d\n", g_config.shipDoorAlwaysOpen ? 1 : 0);
    fprintf(f, "no_teleporter_cooldown=%d\n", g_config.noTeleporterCooldown ? 1 : 0);
    fprintf(f, "instant_teleport=%d\n", g_config.instantTeleport ? 1 : 0);
    fprintf(f, "block_ads=%d\n", g_config.blockAds ? 1 : 0);
    fprintf(f, "grief_teleport_ship=%d\n", g_config.griefTeleportShip ? 1 : 0);
    fprintf(f, "grief_kill=%d\n", g_config.griefKill ? 1 : 0);
    fprintf(f, "grief_open_doors=%d\n", g_config.griefOpenDoors ? 1 : 0);
    fprintf(f, "grief_traps=%d\n", g_config.griefTraps ? 1 : 0);
    fprintf(f, "grief_spawn_enemies=%d\n", g_config.griefSpawnEnemies ? 1 : 0);
    fprintf(f, "grief_boombox=%d\n", g_config.griefBoombox ? 1 : 0);
    fprintf(f, "grief_shuffle_items=%d\n", g_config.griefShuffleItems ? 1 : 0);
    fprintf(f, "anti_kick=%d\n", g_config.antiKick ? 1 : 0);
    fprintf(f, "anti_ban=%d\n", g_config.antiBan ? 1 : 0);
    fprintf(f, "clear_kick_list=%d\n", g_config.clearKickList ? 1 : 0);
    fprintf(f, "esp_players=%d\n", g_config.espPlayers ? 1 : 0);
    fprintf(f, "esp_objects=%d\n", g_config.espObjects ? 1 : 0);
    fprintf(f, "esp_boxes=%d\n", g_config.espBoxes ? 1 : 0);
    fprintf(f, "esp_tracelines=%d\n", g_config.espTracelines ? 1 : 0);
    fprintf(f, "esp_labels=%d\n", g_config.espLabels ? 1 : 0);
    fprintf(f, "esp_val_only=%d\n", g_config.espValuablesOnly ? 1 : 0);
    fprintf(f, "esp_names=%d\n", g_config.espShowNames ? 1 : 0);
    fclose(f);
}
