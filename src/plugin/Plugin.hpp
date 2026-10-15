#pragma once

#include <cstdint>

namespace cc {

constexpr uint32_t kPluginAbi = 1;

enum class PluginKind : uint32_t { Block = 1, Tool = 2, Generator = 3 };

// The editor and the cabinet fill this in before calling the plugin.
// Talk to the game through these functions. Pointers are valid only for that call.
struct PluginHost {
    uint32_t abi = kPluginAbi;
    void* user = nullptr;
    void (*log)(void* user, const char* text) = nullptr;
    void (*addScore)(void* user, int amount) = nullptr;
    void (*hurtHero)(void* user, int amount) = nullptr;
    void (*playSound)(void* user, const char* id) = nullptr;
    void (*setStatus)(void* user, const char* text) = nullptr;
    void (*paintTile)(void* user, int x, int y, int tile) = nullptr;
    void (*placeActor)(void* user, const char* actorId, float x, float y) = nullptr;
    float actorX = 0.f;
    float actorY = 0.f;
    float heroX = 0.f;
    float heroY = 0.f;
    int score = 0;
    int tileX = 0;
    int tileY = 0;
    int stageW = 0;
    int stageH = 0;
    int tileW = 16;
    int tileH = 16;
    float num = 0.f;
    float num2 = 0.f;
    const char* pluginName = "";
};

struct PluginDesc {
    uint32_t abi = kPluginAbi;
    PluginKind kind = PluginKind::Block;
    const char* name = "";
    const char* author = "";
    const char* summary = "";
    const char* title = "";
    void (*onBlock)(PluginHost* host) = nullptr;
    void (*onTool)(PluginHost* host) = nullptr;
    void (*onGenerate)(PluginHost* host) = nullptr;
};

}  // namespace cc

extern "C" cc::PluginDesc* cc_plugin_entry();
