#pragma once
#include <pthread.h>
#include <cstdint>

// ── Shared ESP data buffer: main thread populates, render thread reads & draws ──
#define MAX_ESP_PLAYERS 32
#define MAX_ESP_ITEMS  128

struct ESPPlayer {
    float sx, sy;       // screen position (game-space → physical-space already scaled)
    float dist;         // distance from camera (world units)
    float hp;           // health points
};

struct ESPItem {
    float sx, sy;       // screen position (scaled)
    float dist;
    int   value;        // scrap value ($)
    float weight;       // weight in kg
    bool  isHeavy;
};

struct ESPSharedData {
    pthread_mutex_t mutex;
    ESPPlayer  players[MAX_ESP_PLAYERS];
    int        playerCount;
    ESPItem    items[MAX_ESP_ITEMS];
    int        itemCount;
    bool       dataReady;     // worker thread sets true after populate
    bool       drawPlayers;
    bool       drawObjects;
    bool       drawBoxes;
    bool       drawTracelines;
    bool       drawLabels;
    float      maxDist;
};

extern ESPSharedData g_espData;
