#pragma once
#include <pthread.h>
#include <cstdint>

#define MAX_ESP_PLAYERS 32
#define MAX_ESP_ITEMS  128

struct ESPPlayer {
    float sx, sy;
    float dist;
    float hp;
};

struct ESPItem {
    float sx, sy;
    float dist;
    int   value;
    float weight;
    bool  isHeavy;
    char  name[28];   // itemName (e.g. "Gold bar") — empty if unreadable
};

struct TouchData {
    float x, y;
    bool  isDown;
    bool  isUp;
    bool  isTouching;
    bool  fresh;
};

struct ESPSharedData {
    pthread_mutex_t mutex;
    ESPPlayer  players[MAX_ESP_PLAYERS];
    int        playerCount;
    ESPItem    items[MAX_ESP_ITEMS];
    int        itemCount;
    TouchData  touch;
    bool       dataReady;
    bool       drawPlayers;
    bool       drawObjects;
    bool       drawBoxes;
    bool       drawTracelines;
    bool       drawLabels;
    bool       showNames;
    bool       valuablesOnly;
    float      maxDist;
    float      gameW, gameH;
};

extern ESPSharedData g_espData;

// Initialisation / cleanup (call once from injection entry point)
void ESPInit();
void ESPShutdown();

// Call from main thread (e.g. hooked Update)
void GatherESPData();

// Call from render thread (GL context active)
void RenderESPGLES();

// External trigger (can be called from any thread)
void TriggerESPRefresh();