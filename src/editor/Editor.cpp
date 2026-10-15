#include "editor/Editor.hpp"

#include "cabinet_player_path.h"
#include "common/BinaryIO.hpp"
#include "common/Export.hpp"
#include "common/Synth.hpp"
#include "demo/Demo.hpp"
#include "plugin/Plugins.hpp"

#include <SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace cc {
namespace {

const char* kPhysics[] = {"Platform", "Run and gun", "Vertical shooter", "Horizontal shooter", "Maze", "Top-down"};
const char* kScroll[] = {"Follow", "Locked", "Auto up", "Auto down", "Auto left", "Auto right"};
const char* kComplete[] = {"Touch exit", "Reach end", "Collect all", "Defeat boss"};
const char* kKinds[] = {"Hero", "Enemy", "Gun", "Item", "Check", "Boss", "Decoration"};
const char* kPickups[] = {"None", "Coin", "Heal", "Extra life", "Weapon", "Invincibility", "Key", "Power pellet", "Special"};
const char* kPatterns[] = {"None",     "Idle",   "Patrol", "Chase", "Shooter", "Sine", "Bounce", "Hopper",
                           "Dive",     "Maze ghost", "Boss", "Turret", "Fly", "Platform"};
const char* kAims[] = {"Facing", "At player", "Fixed angle", "Up", "Down"};
const char* kTools[] = {"Select", "Paint", "Erase", "Fill", "Rect", "Stamp"};

struct Swatch {
    uint8_t r, g, b;
};

const Swatch kPalette[] = {
    {0, 0, 0},       {255, 255, 255}, {255, 70, 70},   {255, 180, 60}, {255, 230, 90}, {80, 200, 90},
    {70, 170, 255},  {150, 90, 255},  {255, 120, 180}, {120, 80, 50},  {40, 40, 50},   {90, 90, 110},
    {180, 180, 190}, {20, 180, 170},  {240, 240, 210}, {30, 60, 140},
};

void beginField(const char* label) {
    ImGui::AlignTextToFramePadding();
    const float column = ImGui::GetCursorPosX() + 118.f;
    ImGui::TextUnformatted(label);
    ImGui::SameLine(column);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool editString(const char* label, std::string& value) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    const std::string id = std::string("##") + label;
    beginField(label);
    if (ImGui::InputText(id.c_str(), buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

bool editEnum(const char* label, uint32_t& value, const char* const* names, int count) {
    int current = static_cast<int>(value);
    if (current < 0 || current >= count) current = 0;
    const std::string id = std::string("##") + label;
    beginField(label);
    bool changed = false;
    if (ImGui::BeginCombo(id.c_str(), names[current])) {
        for (int i = 0; i < count; ++i) {
            if (ImGui::Selectable(names[i], i == current)) {
                value = static_cast<uint32_t>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool editFloat(const char* label, float& value) {
    const std::string id = std::string("##") + label;
    beginField(label);
    return ImGui::InputFloat(id.c_str(), &value);
}

bool editInt(const char* label, int& value) {
    const std::string id = std::string("##") + label;
    beginField(label);
    return ImGui::InputInt(id.c_str(), &value);
}

int pageButtons(const char* const* names, int count, int current) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float regionRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    for (int i = 0; i < count; ++i) {
        const float width = ImGui::CalcTextSize(names[i]).x + style.FramePadding.x * 2.f;
        if (i > 0 && ImGui::GetItemRectMax().x + style.ItemSpacing.x + width < regionRight) ImGui::SameLine();
        const bool on = current == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.30f, 0.12f, 1.f));
        if (ImGui::Button(names[i])) current = i;
        if (on) ImGui::PopStyleColor();
    }
    return current;
}

std::string uniqueId(const Project& project, const std::string& base) {
    auto taken = [&](const std::string& id) {
        if (project.findActor(id) || project.findSprite(id) || project.findTileset(id) || project.findSound(id)) return true;
        for (const auto& stage : project.stages)
            if (stage.id == id) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2; i < 999; ++i) {
        const std::string id = base + std::to_string(i);
        if (!taken(id)) return id;
    }
    return base + "_x";
}

constexpr float kLayoutLeft = 320.f;
constexpr float kLayoutInspector = 340.f;
constexpr float kLayoutBottom = 64.f;

std::string lastProjectFile() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return std::string(home) + "/.config/cabinetcore/last-project";
}

std::string layoutFile() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return std::string(home) + "/.config/cabinetcore/layout";
}

float clampLayout(float value, float minValue, float maxValue) {
    if (!(maxValue > minValue)) return minValue;
    return std::clamp(value, minValue, maxValue);
}

bool acceptLayout(float left, float inspector, float bottom, float paint) {
    auto finite = [](float value) { return std::isfinite(value); };
    if (!finite(left) || !finite(inspector) || !finite(bottom) || !finite(paint)) return false;
    if (left < 80.f || left > 4000.f || inspector < 80.f || inspector > 4000.f) return false;
    if (bottom < 24.f || bottom > 2000.f) return false;
    if (paint < -1.5f || paint > 2000.f) return false;
    if (paint >= 0.f && paint < 24.f) return false;
    return true;
}

void layoutSplitter(const char* id, bool vertical, float thickness, float length, float& size, float minSize, float maxSize, bool invert,
                    bool& reset) {
    ImGui::PushID(id);
    const ImVec2 grip(vertical ? thickness : std::max(1.f, length), vertical ? std::max(1.f, length) : thickness);
    ImGui::InvisibleButton("grip", grip);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    reset = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (!reset && active) {
        const float delta = vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
        if (delta != 0.f) size = clampLayout(size + (invert ? -delta : delta), minSize, maxSize);
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImU32 color = IM_COL32(92, 62, 34, 255);
    if (active) color = IM_COL32(245, 160, 50, 255);
    else if (hovered) color = IM_COL32(190, 110, 36, 255);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (vertical) {
        const float mid = (min.x + max.x) * 0.5f;
        draw->AddRectFilled(ImVec2(mid - 1.f, min.y), ImVec2(mid + 1.f, max.y), color);
    } else {
        const float mid = (min.y + max.y) * 0.5f;
        draw->AddRectFilled(ImVec2(min.x, mid - 1.f), ImVec2(max.x, mid + 1.f), color);
    }
    if (hovered && !active) ImGui::SetTooltip("Drag to resize. Double-click for the normal size.");
    ImGui::PopID();
}

void rememberProject(const std::string& path) {
    const std::string file = lastProjectFile();
    if (file.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), error);
    std::ofstream out(file);
    if (out) out << path << '\n';
}

}  // namespace

Editor::Editor() {
    const std::string file = lastProjectFile();
    std::ifstream in(file);
    std::string saved;
    std::getline(in, saved);
    std::string err;
    if (saved.empty() || !std::filesystem::exists(saved) || !openProject(saved, err)) loadDemo();
    loadLayout();
    loadRememberedPlugins();
}

void Editor::loadDemo() {
    project_ = makeDemo();
    path_.clear();
    stageIndex_ = 0;
    selectedPlacement_ = -1;
    status_ = "Demo loaded. Paint with the left mouse. F5 playtests. F6 writes dist/game.dat and dist/cabinet.elf.";
    title_ = "CabinetCore — Neon Cabinet";
}

bool Editor::openProject(const std::string& path, std::string& err) {
    Project loaded;
    if (!Project::load(path, loaded, err)) return false;
    project_ = std::move(loaded);
    path_ = path;
    stageIndex_ = 0;
    selectedPlacement_ = -1;
    undo_.clear();
    redo_.clear();
    status_ = "Opened " + path;
    title_ = "CabinetCore — " + project_.settings.title;
    rememberProject(path);
    return true;
}

Stage* Editor::currentStage() {
    if (stageIndex_ < 0 || stageIndex_ >= static_cast<int>(project_.stages.size())) return nullptr;
    return &project_.stages[stageIndex_];
}

const Stage* Editor::currentStage() const {
    if (stageIndex_ < 0 || stageIndex_ >= static_cast<int>(project_.stages.size())) return nullptr;
    return &project_.stages[stageIndex_];
}

Tileset* Editor::tileset() {
    Stage* stage = currentStage();
    if (!stage) return project_.tilesets.empty() ? nullptr : &project_.tilesets[0];
    if (Tileset* found = project_.findTileset(stage->tilesetId)) return found;
    return project_.tilesets.empty() ? nullptr : &project_.tilesets[0];
}

Sprite* Editor::selectedSprite() {
    if (selectedSprite_ < 0 || selectedSprite_ >= static_cast<int>(project_.sprites.size())) return nullptr;
    return &project_.sprites[selectedSprite_];
}

void Editor::ensureLayers(Stage& stage) {
    const size_t count = static_cast<size_t>(std::max(1, stage.width)) * std::max(1, stage.height);
    if (stage.main.size() != count) stage.main.assign(count, 0);
    if (stage.back.size() != count) stage.back.assign(count, 0);
    if (stage.front.size() != count) stage.front.assign(count, 0);
}

std::vector<uint16_t>& Editor::activeLayer() {
    Stage* stage = currentStage();
    ensureLayers(*stage);
    if (layer_ == 0) return stage->back;
    if (layer_ == 2) return stage->front;
    return stage->main;
}

uint32_t mixHash(uint32_t hash, uint32_t value) {
    hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    return hash;
}

uint32_t mixFloat(uint32_t hash, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return mixHash(hash, bits);
}

uint32_t mixText(uint32_t hash, const std::string& text) {
    return mixHash(hash, crc32(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

uint32_t mixBlock(uint32_t hash, const Block& block) {
    hash = mixHash(hash, static_cast<uint32_t>(block.kind));
    hash = mixFloat(hash, block.num);
    hash = mixFloat(hash, block.num2);
    hash = mixHash(hash, static_cast<uint32_t>(block.pick));
    hash = mixText(hash, block.name);
    for (const auto& child : block.inner) hash = mixBlock(hash, child);
    return hash;
}

uint32_t projectHash(const Project& project) {
    uint32_t hash = 2166136261u;
    hash = mixText(hash, project.settings.title);
    hash = mixText(hash, project.settings.author);
    hash = mixText(hash, project.settings.startStage);
    hash = mixHash(hash, static_cast<uint32_t>(project.settings.viewW));
    hash = mixHash(hash, static_cast<uint32_t>(project.settings.viewH));
    hash = mixHash(hash, static_cast<uint32_t>(project.settings.scale));
    hash = mixHash(hash, static_cast<uint32_t>(project.settings.lives));
    hash = mixHash(hash, static_cast<uint32_t>(project.settings.maxHealth));
    hash = mixHash(hash, project.settings.crt ? 1u : 0u);
    hash = mixHash(hash, project.settings.screenShake ? 1u : 0u);
    hash = mixHash(hash, project.settings.freePlay ? 1u : 0u);
    hash = mixHash(hash, static_cast<uint32_t>(project.nextUid));
    for (const auto& stage : project.stages) {
        hash = mixText(hash, stage.id);
        hash = mixText(hash, stage.name);
        hash = mixHash(hash, static_cast<uint32_t>(stage.width));
        hash = mixHash(hash, static_cast<uint32_t>(stage.height));
        hash = mixText(hash, stage.tilesetId);
        hash = mixText(hash, stage.musicId);
        hash = mixText(hash, stage.message);
        hash = mixHash(hash, static_cast<uint32_t>(stage.physics));
        hash = mixHash(hash, static_cast<uint32_t>(stage.scroll));
        hash = mixHash(hash, static_cast<uint32_t>(stage.complete));
        hash = mixFloat(hash, stage.autoScroll);
        hash = mixFloat(hash, stage.gravity);
        hash = mixFloat(hash, stage.jumpVel);
        hash = mixFloat(hash, stage.playerSpeed);
        hash = mixHash(hash, stage.intermission ? 1u : 0u);
        hash = mixHash(hash, crc32(reinterpret_cast<const uint8_t*>(stage.main.data()), stage.main.size() * sizeof(uint16_t)));
        hash = mixHash(hash, crc32(reinterpret_cast<const uint8_t*>(stage.back.data()), stage.back.size() * sizeof(uint16_t)));
        hash = mixHash(hash, crc32(reinterpret_cast<const uint8_t*>(stage.front.data()), stage.front.size() * sizeof(uint16_t)));
        for (const auto& placement : stage.placements) {
            hash = mixText(hash, placement.actorId);
            hash = mixFloat(hash, placement.x);
            hash = mixFloat(hash, placement.y);
        }
    }
    for (const auto& actor : project.actors) {
        hash = mixText(hash, actor.id);
        hash = mixText(hash, actor.name);
        hash = mixText(hash, actor.spriteId);
        hash = mixText(hash, actor.projectileId);
        hash = mixText(hash, actor.dropActorId);
        hash = mixHash(hash, static_cast<uint32_t>(actor.kind));
        hash = mixHash(hash, static_cast<uint32_t>(actor.pickup));
        hash = mixFloat(hash, actor.hitW);
        hash = mixFloat(hash, actor.hitH);
        hash = mixFloat(hash, actor.dropChance);
        hash = mixHash(hash, static_cast<uint32_t>(actor.pickupAmount));
        hash = mixHash(hash, actor.needsKey ? 1u : 0u);
        hash = mixHash(hash, actor.checkpoint ? 1u : 0u);
        hash = mixHash(hash, static_cast<uint32_t>(actor.behavior.pattern));
        hash = mixHash(hash, static_cast<uint32_t>(actor.behavior.aim));
        hash = mixFloat(hash, actor.behavior.speed);
        hash = mixFloat(hash, actor.behavior.fireInterval);
        hash = mixFloat(hash, actor.behavior.shotSpeed);
        hash = mixFloat(hash, actor.behavior.shotAngle);
        hash = mixFloat(hash, actor.behavior.spread);
        hash = mixFloat(hash, actor.behavior.amplitude);
        hash = mixFloat(hash, actor.behavior.period);
        hash = mixFloat(hash, actor.behavior.lifetime);
        hash = mixHash(hash, static_cast<uint32_t>(actor.behavior.hp));
        hash = mixHash(hash, static_cast<uint32_t>(actor.behavior.touchDamage));
        hash = mixHash(hash, static_cast<uint32_t>(actor.behavior.score));
        hash = mixHash(hash, static_cast<uint32_t>(actor.behavior.burst));
        hash = mixHash(hash, actor.behavior.gravity ? 1u : 0u);
        hash = mixHash(hash, actor.behavior.turnAtEdge ? 1u : 0u);
        hash = mixHash(hash, actor.behavior.turnAtWall ? 1u : 0u);
        hash = mixHash(hash, actor.behavior.stomp ? 1u : 0u);
        for (const auto& prop : actor.props) {
            hash = mixText(hash, prop.name);
            hash = mixFloat(hash, prop.initial);
        }
        for (const auto& script : actor.scripts) {
            hash = mixBlock(hash, script.hat);
            for (const auto& block : script.body) hash = mixBlock(hash, block);
        }
    }
    for (const auto& sprite : project.sprites) {
        hash = mixText(hash, sprite.id);
        hash = mixText(hash, sprite.name);
        hash = mixHash(hash, static_cast<uint32_t>(sprite.frameW));
        hash = mixHash(hash, static_cast<uint32_t>(sprite.frameH));
        hash = mixHash(hash, static_cast<uint32_t>(sprite.frames));
        hash = mixFloat(hash, sprite.fps);
        hash = mixHash(hash, crc32(sprite.rgba.data(), sprite.rgba.size()));
    }
    for (const auto& tile : project.tilesets) {
        hash = mixText(hash, tile.id);
        hash = mixHash(hash, static_cast<uint32_t>(tile.imgW));
        hash = mixHash(hash, static_cast<uint32_t>(tile.imgH));
        hash = mixHash(hash, crc32(tile.rgba.data(), tile.rgba.size()));
        for (const auto& def : tile.tiles) {
            hash = mixHash(hash, def.flags);
            hash = mixHash(hash, static_cast<uint32_t>(def.damage));
            hash = mixHash(hash, static_cast<uint32_t>(def.frames));
            hash = mixFloat(hash, def.fps);
        }
    }
    for (const auto& sound : project.sounds) {
        hash = mixText(hash, sound.id);
        hash = mixText(hash, sound.name);
        hash = mixHash(hash, sound.music ? 1u : 0u);
        hash = mixHash(hash, crc32(reinterpret_cast<const uint8_t*>(sound.samples.data()), sound.samples.size() * sizeof(int16_t)));
    }
    return hash;
}

void Editor::pushState(const Project& state) {
    Snapshot snap;
    snap.project = state;
    snap.stageIndex = stageIndex_;
    undo_.push_back(std::move(snap));
    if (undo_.size() > 25) undo_.erase(undo_.begin());
    redo_.clear();
}

void Editor::pushUndo() {
    if (undoSuspend_) return;
    pushState(project_);
    undoUsed_ = true;
}

void Editor::restore(const Snapshot& snap) {
    project_ = snap.project;
    stageIndex_ = std::clamp(snap.stageIndex, 0, std::max(0, static_cast<int>(project_.stages.size()) - 1));
    if (selectedActor_ >= static_cast<int>(project_.actors.size())) selectedActor_ = project_.actors.empty() ? -1 : 0;
    if (selectedSprite_ >= static_cast<int>(project_.sprites.size())) selectedSprite_ = project_.sprites.empty() ? -1 : 0;
    if (selectedSound_ >= static_cast<int>(project_.sounds.size())) selectedSound_ = -1;
    if (stageIndex_ >= 0 && stageIndex_ < static_cast<int>(project_.stages.size()) &&
        selectedPlacement_ >= static_cast<int>(project_.stages[static_cast<size_t>(stageIndex_)].placements.size()))
        selectedPlacement_ = -1;
    if (blockActor_ < 0 || blockActor_ >= static_cast<int>(project_.actors.size())) blocksOpen_ = false;
    before_ = project_;
    undoReady_ = true;
    undoUsed_ = true;
}

void Editor::undo() {
    if (undo_.empty()) return;
    Snapshot current;
    current.project = project_;
    current.stageIndex = stageIndex_;
    redo_.push_back(std::move(current));
    if (redo_.size() > 25) redo_.erase(redo_.begin());
    const Snapshot snap = undo_.back();
    undo_.pop_back();
    restore(snap);
    status_ = "Undid";
}

void Editor::redo() {
    if (redo_.empty()) return;
    Snapshot current;
    current.project = project_;
    current.stageIndex = stageIndex_;
    undo_.push_back(std::move(current));
    if (undo_.size() > 25) undo_.erase(undo_.begin());
    const Snapshot snap = redo_.back();
    redo_.pop_back();
    restore(snap);
    status_ = "Redid";
}

void Editor::checkLevel() {
    levelNotes_.clear();
    levelChecked_ = true;
    Stage* stage = currentStage();
    if (!stage) {
        levelNotes_.push_back("There is no stage to check.");
        return;
    }
    const Tileset* set = tileset();
    const int tileW = set ? std::max(1, set->tileW) : 16;
    const int tileH = set ? std::max(1, set->tileH) : 16;
    if (stage->main.size() != static_cast<size_t>(stage->width) * stage->height)
        levelNotes_.push_back("The tile map size does not match the stage.");
    auto flagsOf = [&](uint16_t id) -> uint32_t {
        if (id == 0 || !set || id > set->tiles.size()) return 0;
        return set->tiles[id - 1].flags;
    };
    auto flagsAt = [&](int tx, int ty) -> uint32_t {
        if (tx < 0 || ty < 0 || tx >= stage->width || ty >= stage->height) return 0;
        return flagsOf(stage->get(stage->main, tx, ty)) | flagsOf(stage->get(stage->front, tx, ty));
    };
    auto standable = [&](int tx, int ty) {
        const uint32_t flags = flagsAt(tx, ty);
        return (flags & (TileSolid | TileOneWay | TileCrumble)) != 0;
    };
    if (stage->intermission) levelNotes_.push_back("This stage is a message screen.");
    int heroes = 0;
    bool exit = false;
    bool coin = false;
    bool boss = false;
    bool key = false;
    bool door = false;
    const float mapW = static_cast<float>(stage->width * tileW);
    const float mapH = static_cast<float>(stage->height * tileH);
    for (const auto& placement : stage->placements) {
        const ActorDef* actor = project_.findActor(placement.actorId);
        if (!actor) {
            levelNotes_.push_back("A placed actor id is missing: " + placement.actorId);
            continue;
        }
        if (placement.x < -tileW || placement.y < -tileH || placement.x > mapW || placement.y > mapH)
            levelNotes_.push_back(actor->name + " sits outside the map.");
        if (!actor->spriteId.empty() && !project_.findSprite(actor->spriteId))
            levelNotes_.push_back(actor->name + " points at a missing sprite.");
        if (actor->spriteId.empty() && actor->kind != ActorKind::Gun)
            levelNotes_.push_back(actor->name + " has no sprite.");
        if (!actor->projectileId.empty() && !project_.findActor(actor->projectileId))
            levelNotes_.push_back(actor->name + " shoots a missing actor.");
        if ((actor->behavior.pattern == AiPattern::Shooter || actor->behavior.pattern == AiPattern::Turret ||
             actor->kind == ActorKind::Boss) &&
            actor->projectileId.empty() && actor->kind != ActorKind::Hero)
            levelNotes_.push_back(actor->name + " attacks, but has no projectile.");
        if (actor->kind == ActorKind::Hero) {
            ++heroes;
            const int tx = static_cast<int>((placement.x + actor->hitW * 0.5f) / tileW);
            const int ty = static_cast<int>((placement.y + actor->hitH * 0.5f) / tileH);
            const uint32_t flags = flagsAt(tx, ty);
            if (flags & TileSolid) levelNotes_.push_back("The hero starts inside a solid tile.");
            if (flags & TileHazard) levelNotes_.push_back("The hero starts on a hazard.");
        }
        if (actor->kind == ActorKind::Check && !actor->checkpoint) exit = true;
        if (actor->kind == ActorKind::Item && (actor->pickup == Pickup::Coin || actor->pickup == Pickup::PowerPellet)) coin = true;
        if (actor->kind == ActorKind::Boss) boss = true;
        if (actor->pickup == Pickup::Key) key = true;
        if (actor->needsKey) door = true;
    }
    if (!stage->intermission && heroes == 0) levelNotes_.push_back("Place a hero on this stage.");
    if (heroes > 1) levelNotes_.push_back("More than one hero is placed.");
    if (!stage->intermission && stage->complete == CompleteMode::TouchExit && !exit)
        levelNotes_.push_back("The stage ends at an exit, and none is placed.");
    if (stage->complete == CompleteMode::CollectAll && !coin)
        levelNotes_.push_back("The stage asks for every coin, and none is placed.");
    if (stage->complete == CompleteMode::BossDead && !boss)
        levelNotes_.push_back("The stage ends when a boss dies, and none is placed.");
    if (door && !key) levelNotes_.push_back("A door needs a key, and none is placed.");
    if (!stage->musicId.empty() && !project_.findSound(stage->musicId))
        levelNotes_.push_back("The music id does not match a sound.");
    bool stand = false;
    for (int y = 0; y < stage->height && !stand; ++y)
        for (int x = 0; x < stage->width; ++x)
            if (standable(x, y)) stand = true;
    if (!stage->intermission && (stage->physics == PhysicsMode::Platform || stage->physics == PhysicsMode::RunAndGun) && !stand)
        levelNotes_.push_back("A platform stage needs a solid or one-way tile to stand on.");
    for (int x = 0; x < stage->width; ++x) {
        int run = 0;
        int top = 0;
        auto finishRun = [&]() {
            if (run < 3) return;
            const bool landing = standable(x - 1, top) || standable(x + 1, top) || standable(x - 1, top - 1) ||
                                 standable(x + 1, top - 1) || standable(x, top - 1) || standable(x - 1, top + 1) ||
                                 standable(x + 1, top + 1);
            if (!landing)
                levelNotes_.push_back("The ladder at column " + std::to_string(x) + " has no landing at the top.");
        };
        for (int y = 0; y < stage->height; ++y) {
            if (flagsAt(x, y) & TileLadder) {
                if (run == 0) top = y;
                ++run;
            } else {
                finishRun();
                run = 0;
            }
        }
        finishRun();
    }
    if (levelNotes_.empty()) levelNotes_.push_back("This stage looks ready.");
}

void Editor::trackUndo() {
    const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                      ImGui::IsAnyItemActive();
    const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    if (!undoReady_) {
        before_ = project_;
        undoReady_ = true;
    }
    if (down) {
        undoBusy_ = true;
        return;
    }
    if (!(undoBusy_ || released || undoUsed_)) return;
    if (!undoUsed_ && projectHash(before_) != projectHash(project_)) {
        pushState(before_);
        before_ = project_;
    } else if (undoUsed_) {
        before_ = project_;
    }
    undoBusy_ = false;
    undoUsed_ = false;
}

void Editor::flood(int tx, int ty, uint16_t from, uint16_t to) {
    Stage* stage = currentStage();
    if (!stage || from == to) return;
    std::vector<std::pair<int, int>> stack;
    stack.push_back({tx, ty});
    int guard = stage->width * stage->height * 8 + 8;
    while (!stack.empty() && guard-- > 0) {
        const int x = stack.back().first;
        const int y = stack.back().second;
        stack.pop_back();
        if (x < 0 || y < 0 || x >= stage->width || y >= stage->height) continue;
        if (stage->get(activeLayer(), x, y) != from) continue;
        stage->set(activeLayer(), x, y, to);
        stack.push_back({x + 1, y});
        stack.push_back({x - 1, y});
        stack.push_back({x, y + 1});
        stack.push_back({x, y - 1});
    }
}

int Editor::placementAt(float x, float y) const {
    const Stage* stage = currentStage();
    if (!stage) return -1;
    for (int i = static_cast<int>(stage->placements.size()) - 1; i >= 0; --i) {
        const Placement& placement = stage->placements[static_cast<size_t>(i)];
        const ActorDef* actor = project_.findActor(placement.actorId);
        const Sprite* sprite = actor ? project_.findSprite(actor->spriteId) : nullptr;
        const float w = std::max(12.f, sprite ? static_cast<float>(sprite->frameW) : 16.f);
        const float h = std::max(12.f, sprite ? static_cast<float>(sprite->frameH) : 16.f);
        if (x >= placement.x && y >= placement.y && x < placement.x + w && y < placement.y + h) return i;
    }
    return -1;
}

void Editor::applyTool(int tx, int ty, bool click, bool down) {
    Stage* stage = currentStage();
    if (!stage) return;
    if (tx < 0 || ty < 0 || tx >= stage->width || ty >= stage->height) return;
    const uint16_t tile = tool_ == 2 ? 0 : static_cast<uint16_t>(std::max(0, selectedTile_));
    if (tool_ == 1 || tool_ == 2) {
        stage->set(activeLayer(), tx, ty, tile);
    } else if (tool_ == 3 && click) {
        flood(tx, ty, stage->get(activeLayer(), tx, ty), tile);
    } else if (tool_ == 4 && click) {
        dragX_ = tx;
        dragY_ = ty;
    } else if (tool_ == 4 && !down && dragX_ >= 0) {
        const int x0 = std::min(dragX_, tx);
        const int y0 = std::min(dragY_, ty);
        const int x1 = std::max(dragX_, tx);
        const int y1 = std::max(dragY_, ty);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) stage->set(activeLayer(), x, y, tile);
        dragX_ = dragY_ = -1;
    } else if (tool_ == 5 && click) {
        if (selectedActor_ < 0 || selectedActor_ >= static_cast<int>(project_.actors.size())) return;
        const Tileset* set = tileset();
        const int tileW = set ? set->tileW : 16;
        const int tileH = set ? set->tileH : 16;
        Placement placement;
        placement.uid = project_.freshUid();
        placement.actorId = project_.actors[selectedActor_].id;
        placement.x = tx * static_cast<float>(tileW);
        placement.y = ty * static_cast<float>(tileH);
        stage->placements.push_back(placement);
        selectedPlacement_ = static_cast<int>(stage->placements.size()) - 1;
        status_ = "Stamped " + project_.actors[selectedActor_].name;
    } else if (tool_ == 0 && click) {
        const Tileset* set = tileset();
        const float tileW = set ? static_cast<float>(set->tileW) : 16.f;
        const float tileH = set ? static_cast<float>(set->tileH) : 16.f;
        selectedPlacement_ = -1;
        float best = 18.f * 18.f;
        for (int i = 0; i < static_cast<int>(stage->placements.size()); ++i) {
            const float dx = stage->placements[i].x - tx * tileW;
            const float dy = stage->placements[i].y - ty * tileH;
            const float d = dx * dx + dy * dy;
            if (d < best) {
                best = d;
                selectedPlacement_ = i;
            }
        }
    }
}

void Editor::paintMap(SDL_Renderer* renderer) {
    Stage* stage = currentStage();
    if (!stage || !renderer) return;
    ensureLayers(*stage);
    const Tileset* set = tileset();
    const int tileW = set && set->tileW > 0 ? set->tileW : 16;
    const int tileH = set && set->tileH > 0 ? set->tileH : 16;
    const int width = std::max(1, stage->width * tileW);
    const int height = std::max(1, stage->height * tileH);
    map_.resize(width, height);
    map_.clear(16, 14, 28);
    auto blitTile = [&](uint16_t id, int dx, int dy, uint8_t alpha) {
        if (id == 0) return;
        if (!set || set->rgba.empty()) {
            map_.fillRect(dx, dy, tileW, tileH, 120, 90, 70, alpha);
            return;
        }
        const int cols = std::max(1, set->columns);
        const int cell = id - 1;
        const int sx = (cell % cols) * tileW;
        const int sy = (cell / cols) * tileH;
        map_.blit(set->rgba.data(), set->imgW, set->imgH, sx, sy, tileW, tileH, dx, dy, false);
        (void)alpha;
    };
    for (int y = 0; y < stage->height; ++y) {
        for (int x = 0; x < stage->width; ++x) {
            blitTile(stage->get(stage->back, x, y), x * tileW, y * tileH, 255);
            blitTile(stage->get(stage->main, x, y), x * tileW, y * tileH, 255);
        }
    }
    for (int i = 0; i < static_cast<int>(stage->placements.size()); ++i) {
        const Placement& placement = stage->placements[i];
        const ActorDef* actor = project_.findActor(placement.actorId);
        const Sprite* sprite = actor ? project_.findSprite(actor->spriteId) : nullptr;
        const int dx = static_cast<int>(placement.x);
        const int dy = static_cast<int>(placement.y);
        if (sprite && !sprite->rgba.empty()) {
            map_.blit(sprite->rgba.data(), sprite->imgW, sprite->imgH, 0, 0, sprite->frameW, sprite->frameH, dx, dy, false);
        } else {
            map_.fillRect(dx, dy, 16, 16, 255, 80, 80);
        }
        if (i == selectedPlacement_) map_.fillRect(dx, dy, sprite ? sprite->frameW : 16, 2, 255, 220, 80);
    }
    for (int y = 0; y < stage->height; ++y)
        for (int x = 0; x < stage->width; ++x)
            blitTile(stage->get(stage->front, x, y), x * tileW, y * tileH, 255);
    if (showCollision_ && set) {
        for (int y = 0; y < stage->height; ++y) {
            for (int x = 0; x < stage->width; ++x) {
                auto flagsOf = [&](int id) -> uint32_t {
                    if (id <= 0 || id > static_cast<int>(set->tiles.size())) return 0;
                    return set->tiles[static_cast<size_t>(id - 1)].flags;
                };
                const uint32_t flags = flagsOf(stage->get(stage->main, x, y)) | flagsOf(stage->get(stage->front, x, y));
                uint8_t r = 0, g = 0, b = 0, a = 0;
                if (flags & TileHazard) {
                    r = 255;
                    g = 40;
                    b = 170;
                    a = 120;
                } else if (flags & TileLadder) {
                    r = 255;
                    g = 210;
                    b = 40;
                    a = 120;
                } else if (flags & TileCrumble) {
                    r = 255;
                    g = 140;
                    b = 40;
                    a = 120;
                } else if (flags & TileOneWay) {
                    r = 40;
                    g = 210;
                    b = 80;
                    a = 110;
                } else if (flags & TileSolid) {
                    r = 255;
                    g = 70;
                    b = 50;
                    a = 100;
                } else if (flags & TileWater) {
                    r = 50;
                    g = 130;
                    b = 255;
                    a = 90;
                }
                if (a) map_.fillRect(x * tileW, y * tileH, tileW, tileH, r, g, b, a);
            }
        }
    }
    if (grid_ && zoom_ >= 2) {
        for (int x = 0; x < width; x += tileW)
            for (int y = 0; y < height; ++y) map_.pset(x, y, 255, 255, 255, 28);
        for (int y = 0; y < height; y += tileH)
            for (int x = 0; x < width; ++x) map_.pset(x, y, 255, 255, 255, 28);
    }
    if (!mapTex_ || mapTexW_ != width || mapTexH_ != height) {
        if (mapTex_) SDL_DestroyTexture(mapTex_);
        mapTex_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
        mapTexW_ = width;
        mapTexH_ = height;
        if (mapTex_) SDL_SetTextureScaleMode(mapTex_, SDL_ScaleModeNearest);
    }
    if (mapTex_) SDL_UpdateTexture(mapTex_, nullptr, map_.pixels().data(), width * 4);
    if (set && !set->rgba.empty() && set->imgW > 0 && set->imgH > 0) {
        if (!tilesetTex_ || tilesetTexW_ != set->imgW || tilesetTexH_ != set->imgH) {
            if (tilesetTex_) SDL_DestroyTexture(tilesetTex_);
            tilesetTex_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, set->imgW, set->imgH);
            tilesetTexW_ = set->imgW;
            tilesetTexH_ = set->imgH;
            if (tilesetTex_) {
                SDL_SetTextureScaleMode(tilesetTex_, SDL_ScaleModeNearest);
                SDL_SetTextureBlendMode(tilesetTex_, SDL_BLENDMODE_BLEND);
            }
        }
        if (tilesetTex_) SDL_UpdateTexture(tilesetTex_, nullptr, set->rgba.data(), set->imgW * 4);
    }
}

void Editor::drawMenu() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::BeginMenu("New")) {
            auto beginNew = [&](Project project) {
                project_ = std::move(project);
                path_.clear();
                stageIndex_ = 0;
                selectedPlacement_ = -1;
                selectedActor_ = 0;
                undo_.clear();
                redo_.clear();
                blocksOpen_ = false;
                title_ = "CabinetCore — " + project_.settings.title;
                status_ = "New project. Ctrl+S saves it, and the editor reopens that file next time.";
            };
            if (ImGui::MenuItem("Platform")) beginNew(makeTemplate(PhysicsMode::Platform, "Platform"));
            if (ImGui::MenuItem("Run and gun")) beginNew(makeTemplate(PhysicsMode::RunAndGun, "Run and Gun"));
            if (ImGui::MenuItem("Vertical shooter")) beginNew(makeTemplate(PhysicsMode::ShooterVert, "Vertical Shooter"));
            if (ImGui::MenuItem("Horizontal shooter")) beginNew(makeTemplate(PhysicsMode::ShooterHorz, "Horizontal Shooter"));
            if (ImGui::MenuItem("Maze")) beginNew(makeTemplate(PhysicsMode::Maze, "Maze"));
            if (ImGui::MenuItem("Top-down")) beginNew(makeTemplate(PhysicsMode::TopDown, "Top Down"));
            if (ImGui::MenuItem("Full demo")) loadDemo();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) openFilePick(FilePick::OpenProject);
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            if (path_.empty()) openFilePick(FilePick::SaveProject);
            else saveProject(path_);
        }
        if (ImGui::MenuItem("Save As...")) openFilePick(FilePick::SaveProject);
        if (ImGui::MenuItem("Build game", "F6")) buildGame();
        if (ImGui::MenuItem("Load plugin...")) openFilePick(FilePick::LoadPlugin);
        if (ImGui::MenuItem("Quit")) status_ = "Close the window to quit.";
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z")) undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y")) redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Grid", nullptr, &grid_);
        ImGui::MenuItem("Snap to grid", nullptr, &snapActors_);
        ImGui::MenuItem("Collision colors", nullptr, &showCollision_);
        ImGui::MenuItem("Hitboxes in playtest", "F3", &showHitboxes_);
        if (ImGui::MenuItem("Zoom in", "Ctrl+Wheel")) zoomIn();
        if (ImGui::MenuItem("Zoom out", "Ctrl+Wheel")) zoomOut();
        ImGui::Separator();
        if (ImGui::MenuItem("Save layout")) saveLayout();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep these panel sizes. The editor opens with them next time.");
        if (ImGui::MenuItem("Reset layout")) resetLayout();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put the tools, scene, inspector, and bottom panel back to their normal sizes.");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Test")) {
        if (ImGui::MenuItem("Editor")) runSelfTest(nullptr, 0);
        if (ImGui::MenuItem("Gameplay")) runSelfTest(nullptr, 1);
        if (ImGui::MenuItem("UI")) runSelfTest(nullptr, 2);
        if (ImGui::MenuItem("Run all")) runSelfTest(nullptr, -1);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

float Editor::snapCoord(float value, int tile) const {
    if (!snapActors_ || tile <= 0) return value;
    const float step = static_cast<float>(tile);
    return std::round(value / step) * step;
}

void Editor::zoomIn() { zoom_ = std::min(6, zoom_ + 1); }

void Editor::zoomOut() { zoom_ = std::max(1, zoom_ - 1); }

void Editor::zoomWheel(float wheel) {
    if (wheel > 0.f) zoomIn();
    else if (wheel < 0.f) zoomOut();
}

void Editor::loadLayout() {
    const std::string file = layoutFile();
    if (file.empty()) return;
    std::ifstream in(file);
    std::string header;
    if (!std::getline(in, header) || header != "CCLAYOUT1") return;
    float left = 0.f, inspector = 0.f, bottom = 0.f, paint = 0.f;
    if (!(in >> left >> inspector >> bottom >> paint) || !acceptLayout(left, inspector, bottom, paint)) return;
    layoutLeft_ = left;
    layoutInspector_ = inspector;
    layoutBottom_ = bottom;
    layoutBottomPaint_ = paint;
}

void Editor::saveLayout() {
    const std::string file = layoutFile();
    if (file.empty()) {
        status_ = "Could not save the layout.";
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), error);
    std::ofstream out(file);
    if (!out) {
        status_ = "Could not save the layout.";
        return;
    }
    out << "CCLAYOUT1\n"
        << std::lround(layoutLeft_) << ' ' << std::lround(layoutInspector_) << ' ' << std::lround(layoutBottom_) << ' '
        << std::lround(layoutBottomPaint_) << '\n';
    status_ = "Layout saved. The editor opens with these panel sizes next time.";
}

void Editor::resetLayout() {
    layoutLeft_ = kLayoutLeft;
    layoutInspector_ = kLayoutInspector;
    layoutBottom_ = kLayoutBottom;
    layoutBottomPaint_ = -1.f;
    const std::string file = layoutFile();
    if (!file.empty()) {
        std::error_code error;
        std::filesystem::remove(file, error);
    }
    status_ = "Layout is back to normal.";
}

void Editor::drawToolbar() {
    for (int i = 0; i < 6; ++i) {
        if (i) ImGui::SameLine();
        const bool selected = tool_ == i;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.48f, 0.14f, 1.f));
        if (ImGui::Button(kTools[i])) tool_ = i;
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    if (ImGui::Button("Playtest  F5")) togglePlay();
    ImGui::SameLine();
    if (ImGui::Button("Build  F6")) buildGame();

    ImGui::RadioButton("Backdrop", &layer_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Collision", &layer_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Foreground", &layer_, 2);
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    if (ImGui::Button("-##zoomout")) zoomOut();
    ImGui::SameLine();
    ImGui::Text("Zoom %d", zoom_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+Wheel over the scene zooms in and out.");
    ImGui::SameLine();
    if (ImGui::Button("+##zoomin")) zoomIn();
    ImGui::SameLine();
    ImGui::Checkbox("Hitboxes", &showHitboxes_);
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &snapActors_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Actors and the hero land on the tile grid while you move them.");
    if (!hoverHint_.empty()) {
        const float width = ImGui::CalcTextSize(hoverHint_.c_str()).x;
        const float right = ImGui::GetWindowWidth() - width - 12.f;
        if (ImGui::GetCursorPosX() + 24.f < right) {
            ImGui::SameLine(right);
            ImGui::TextDisabled("%s", hoverHint_.c_str());
        }
    }
}

uint16_t Editor::findTile(uint32_t need, uint32_t avoid) {
    const Tileset* set = tileset();
    if (!set) return 0;
    for (int i = 0; i < static_cast<int>(set->tiles.size()); ++i) {
        const uint32_t flags = set->tiles[static_cast<size_t>(i)].flags;
        if ((flags & need) == need && (flags & avoid) == 0) return static_cast<uint16_t>(i + 1);
    }
    return 0;
}

bool Editor::standable(const Stage& stage, int x, int y) {
    const uint16_t id = stage.get(stage.main, x, y);
    if (id == 0) return false;
    const Tileset* set = tileset();
    if (!set || id > static_cast<int>(set->tiles.size())) return false;
    const uint32_t flags = set->tiles[id - 1].flags;
    if (flags & (TileLadder | TileHazard | TileWater)) return false;
    return (flags & (TileSolid | TileOneWay | TileCrumble)) != 0;
}

bool Editor::placeActor(Stage& stage, const ActorDef* actor, int tx, int ty) {
    if (!actor || tx < 0 || ty < 0 || tx >= stage.width || ty >= stage.height) return false;
    const Tileset* set = tileset();
    const float tileW = set ? static_cast<float>(set->tileW) : 16.f;
    const float tileH = set ? static_cast<float>(set->tileH) : 16.f;
    const float x = tx * tileW;
    const float y = ty * tileH;
    for (const auto& placement : stage.placements) {
        if (std::abs(placement.x - x) < tileW * 0.5f && std::abs(placement.y - y) < tileH * 0.5f) return false;
    }
    Placement placement;
    placement.uid = project_.freshUid();
    placement.actorId = actor->id;
    placement.x = x;
    placement.y = y;
    stage.placements.push_back(std::move(placement));
    return true;
}

void Editor::generateStretch(int x, int y, int length) {
    Stage* stage = currentStage();
    if (!stage) return;
    length = std::clamp(length, 1, 80);
    x = std::clamp(x, 0, stage->width - 1);
    y = std::clamp(y, 0, stage->height - 1);
    const uint16_t solid = findTile(TileSolid, TileOneWay | TileLadder | TileHazard | TileWater);
    const uint16_t solidAny = solid != 0 ? solid : findTile(TileSolid, 0);
    const uint16_t ladder = findTile(TileLadder, TileSolid | TileHazard);
    const uint16_t oneWay = findTile(TileOneWay, TileHazard | TileLadder);
    const uint16_t hazard = findTile(TileHazard, TileSolid);
    if (genShape_ == 3 && ladder == 0) {
        status_ = "This tileset has no ladder tile.";
        return;
    }
    if (genShape_ != 3 && solidAny == 0) {
        status_ = "This tileset has no solid tile.";
        return;
    }
    const ActorDef* coin = project_.findActor("coin");
    if (!coin) {
        for (const auto& actor : project_.actors)
            if (actor.pickup == Pickup::Coin) {
                coin = &actor;
                break;
            }
    }
    const ActorDef* door = project_.findActor("door");
    if (!door) {
        for (const auto& actor : project_.actors)
            if (actor.kind == ActorKind::Check && !actor.checkpoint) {
                door = &actor;
                break;
            }
    }
    pushUndo();
    genX_ = x;
    genY_ = y;
    genLen_ = length;
    int coins = 0;
    int exits = 0;
    auto coinAt = [&](int tx, int ty, bool last) {
        if (last && genExit_ && door) {
            if (placeActor(*stage, door, tx, ty)) exits++;
            return;
        }
        if (genCoins_ && coin && placeActor(*stage, coin, tx, ty)) coins++;
    };
    if (genShape_ == 0) {
        for (int i = 0; i < length; ++i) stage->set(stage->main, x + i, y, solidAny);
        for (int i = 0; i < length; i += 2) {
            if (i == length - 1) continue;
            coinAt(x + i, y - 1, false);
        }
        if (genExit_ && door && placeActor(*stage, door, x + length - 1, y - 1)) exits++;
        else coinAt(x + length - 1, y - 1, false);
    } else if (genShape_ == 1) {
        const bool pads = length >= 3;
        for (int i = 0; i < length; ++i) {
            const bool pad = pads && (i == 0 || i == length - 1);
            if (pad) stage->set(stage->main, x + i, y, solidAny);
            else {
                stage->set(stage->main, x + i, y, 0);
                if (hazard) stage->set(stage->main, x + i, y + 1, hazard);
            }
        }
        if (pads) {
            coinAt(x, y - 1, false);
            if (genExit_ && door && placeActor(*stage, door, x + length - 1, y - 1)) exits++;
            else coinAt(x + length - 1, y - 1, false);
        }
    } else if (genShape_ == 2) {
        for (int i = 0; i < length; ++i) {
            const int top = std::max(0, y - i);
            for (int yy = top; yy <= y; ++yy) stage->set(stage->main, x + i, yy, solidAny);
            if (i % 2 == 0 || i == length - 1) coinAt(x + i, top - 1, i == length - 1);
        }
    } else if (genShape_ == 3) {
        const int top = std::max(0, y - length + 1);
        for (int yy = top; yy <= y; ++yy) stage->set(stage->main, x, yy, ladder);
        const int ledgeY = std::min(y, top + 1);
        const int dir = x >= 5 ? -1 : 1;
        const uint16_t ledge = oneWay != 0 ? oneWay : solidAny;
        if (ledge != 0) {
            for (int i = 1; i <= 4; ++i) stage->set(stage->main, x + dir * i, ledgeY, ledge);
            for (int i = 1; i <= 4; ++i) coinAt(x + dir * i, ledgeY - 1, i == 4);
        }
    } else {
        for (int i = 0; i < length; ++i) stage->set(stage->main, x, y + i, solidAny);
        const int side = x > 0 ? x - 1 : x + 1;
        for (int i = 0; i < length; i += 2) coinAt(side, y + i, i + 2 >= length);
    }
    const char* names[] = {"Floor", "Pit", "Stairs", "Ladder", "Column"};
    status_ = std::string(names[std::clamp(genShape_, 0, 4)]) + " painted on the collision layer.";
    if (genCoins_) status_ += " Coins: " + std::to_string(coins) + ".";
    if (genExit_) status_ += " Exits: " + std::to_string(exits) + ".";
    if (genDress_) {
        dressStretch(false);
        status_ += " Dressed with ground, a top edge, and a backdrop.";
    }
}

void Editor::populateFloor() {
    Stage* stage = currentStage();
    if (!stage) return;
    if (popActor_ < 0 || popActor_ >= static_cast<int>(project_.actors.size())) {
        status_ = "Choose an actor to place.";
        return;
    }
    const ActorDef* actor = &project_.actors[static_cast<size_t>(popActor_)];
    if (actor->kind == ActorKind::Hero || actor->kind == ActorKind::Gun) {
        status_ = "Pick an enemy, item, or door.";
        return;
    }
    const int x0 = std::clamp(popX_, 0, stage->width - 1);
    const int y0 = std::clamp(popY_, 0, stage->height - 1);
    const int x1 = std::clamp(popX_ + std::max(1, popW_) - 1, x0, stage->width - 1);
    const int y1 = std::clamp(popY_ + std::max(1, popH_) - 1, y0, stage->height - 1);
    std::vector<std::pair<int, int>> spots;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (y - 1 < y0) continue;
            if (standable(*stage, x, y) && !standable(*stage, x, y - 1)) spots.push_back({x, y - 1});
        }
    }
    if (spots.empty()) {
        status_ = "No floor in that area.";
        return;
    }
    const int count = std::clamp(popCount_, 1, static_cast<int>(spots.size()));
    pushUndo();
    int placed = 0;
    for (int i = 0; i < count; ++i) {
        const int index = count == 1 ? static_cast<int>(spots.size()) / 2 : i * (static_cast<int>(spots.size()) - 1) / (count - 1);
        if (placeActor(*stage, actor, spots[static_cast<size_t>(index)].first, spots[static_cast<size_t>(index)].second))
            placed++;
    }
    status_ = "Placed " + std::to_string(placed) + " " + actor->name + " on the floor.";
}

void Editor::populateTurret() {
    Stage* stage = currentStage();
    if (!stage) return;
    const ActorDef* turret = project_.findActor("turret");
    if (!turret) {
        for (const auto& actor : project_.actors) {
            if (actor.behavior.pattern == AiPattern::Turret || actor.behavior.pattern == AiPattern::Shooter) {
                turret = &actor;
                break;
            }
        }
    }
    if (!turret) {
        for (const auto& actor : project_.actors)
            if (actor.kind == ActorKind::Enemy) {
                turret = &actor;
                break;
            }
    }
    if (!turret) {
        status_ = "Add an enemy first.";
        return;
    }
    const int x0 = std::clamp(popX_, 0, stage->width - 1);
    const int y0 = std::clamp(popY_, 0, stage->height - 1);
    const int x1 = std::clamp(popX_ + std::max(1, popW_) - 1, x0, stage->width - 1);
    const int y1 = std::clamp(popY_ + std::max(1, popH_) - 1, y0, stage->height - 1);
    std::vector<std::pair<int, int>> spots;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (y - 1 >= y0 && standable(*stage, x, y) && !standable(*stage, x, y - 1)) spots.push_back({x, y - 1});
    const uint16_t oneWay = findTile(TileOneWay, TileHazard | TileLadder);
    const uint16_t tile = oneWay != 0 ? oneWay : findTile(TileSolid, TileLadder | TileHazard | TileWater);
    if (spots.empty() && tile == 0) {
        status_ = "This tileset has no platform tile.";
        return;
    }
    pushUndo();
    if (!spots.empty()) {
        const auto spot = spots[spots.size() / 2];
        placeActor(*stage, turret, spot.first, spot.second);
        status_ = "Placed " + turret->name + " on a platform in the area.";
        return;
    }
    const int py = std::clamp(y0 + std::max(1, (y1 - y0) / 2), 1, stage->height - 1);
    const int px = std::clamp(x0 + std::max(0, (x1 - x0) / 2 - 1), 0, stage->width - 1);
    for (int i = 0; i < 4; ++i) stage->set(stage->main, px + i, py, tile);
    placeActor(*stage, turret, px + 1, py - 1);
    status_ = "Built a platform and placed " + turret->name + ".";
}

void Editor::populateKeyDoor() {
    Stage* stage = currentStage();
    if (!stage) return;
    const ActorDef* key = project_.findActor("key");
    if (!key) {
        for (const auto& actor : project_.actors)
            if (actor.pickup == Pickup::Key) {
                key = &actor;
                break;
            }
    }
    const ActorDef* door = project_.findActor("door");
    if (!door) {
        for (const auto& actor : project_.actors)
            if (actor.kind == ActorKind::Check && !actor.checkpoint) {
                door = &actor;
                break;
            }
    }
    if (!key && !door) {
        status_ = "This project has no key or door.";
        return;
    }
    const int x0 = std::clamp(popX_, 0, stage->width - 1);
    const int y0 = std::clamp(popY_, 0, stage->height - 1);
    const int x1 = std::clamp(popX_ + std::max(1, popW_) - 1, x0, stage->width - 1);
    const int y1 = std::clamp(popY_ + std::max(1, popH_) - 1, y0, stage->height - 1);
    std::vector<std::pair<int, int>> spots;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (y - 1 >= 0 && standable(*stage, x, y) && !standable(*stage, x, y - 1)) spots.push_back({x, y - 1});
    pushUndo();
    bool placedKey = false;
    bool placedDoor = false;
    if (!spots.empty()) {
        if (key) {
            for (const auto& spot : spots)
                if (placeActor(*stage, key, spot.first, spot.second)) {
                    placedKey = true;
                    break;
                }
        }
        if (door) {
            for (auto spot = spots.rbegin(); spot != spots.rend(); ++spot)
                if (placeActor(*stage, door, spot->first, spot->second)) {
                    placedDoor = true;
                    break;
                }
        }
    } else {
        int keyX = x0;
        int doorX = x1;
        const int standY = std::max(0, y1 - 1);
        if (key && door && keyX == doorX) {
            if (doorX + 1 < stage->width) doorX++;
            else if (keyX > 0) keyX--;
        }
        const uint16_t solid = findTile(TileSolid, TileOneWay | TileLadder | TileHazard | TileWater);
        const uint16_t tile = solid != 0 ? solid : findTile(TileSolid, 0);
        if (tile) {
            stage->set(stage->main, keyX, standY + 1, tile);
            stage->set(stage->main, doorX, standY + 1, tile);
        }
        if (key) placedKey = placeActor(*stage, key, keyX, standY);
        if (door) placedDoor = placeActor(*stage, door, doorX, standY);
    }
    if (placedKey && placedDoor) status_ = "Placed the key and the door.";
    else if (placedKey || placedDoor) status_ = "Placed what would fit.";
    else status_ = "No open spot for the key and door.";
}

void Editor::finishGeneratorDrag() {
    Stage* stage = currentStage();
    genDown_ = false;
    if (!stage) {
        genDrag_ = 0;
        return;
    }
    const int x0 = std::clamp(std::min(genX0_, genX1_), 0, stage->width - 1);
    const int x1 = std::clamp(std::max(genX0_, genX1_), 0, stage->width - 1);
    const int y0 = std::clamp(std::min(genY0_, genY1_), 0, stage->height - 1);
    const int y1 = std::clamp(std::max(genY0_, genY1_), 0, stage->height - 1);
    const int mode = genDrag_;
    genDrag_ = 0;
    if (mode == 2) {
        popX_ = x0;
        popY_ = y0;
        popW_ = x1 - x0 + 1;
        popH_ = y1 - y0 + 1;
        status_ = "Area set. Choose what to place.";
        return;
    }
    if (mode == 3) {
        copyChunk(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
        return;
    }
    if (mode == 4) {
        stampChunk(std::clamp(genX0_, 0, stage->width - 1), std::clamp(genY0_, 0, stage->height - 1));
        return;
    }
    if (genShape_ == 4) {
        genX_ = x0;
        genY_ = y0;
        genLen_ = y1 - y0 + 1;
    } else if (genShape_ == 3) {
        genX_ = x0;
        genY_ = y1;
        genLen_ = y1 - y0 + 1;
    } else {
        genX_ = x0;
        genY_ = y1;
        genLen_ = x1 - x0 + 1;
    }
    generateStretch(genX_, genY_, genLen_);
}

namespace {

void ensureProp(ActorDef& actor, const std::string& name, float value) {
    for (const auto& prop : actor.props)
        if (prop.name == name) return;
    actor.props.push_back({name, value});
}

}  // namespace

void Editor::giveDoubleJump(ActorDef& actor) {
    ensureProp(actor, "jumps", 1.f);
    ensureProp(actor, "grounded", 1.f);
    Script every;
    every.hat.kind = BlockKind::EveryFrame;
    Block clear;
    clear.kind = BlockKind::SetProp;
    clear.name = "grounded";
    every.body.push_back(clear);
    Block grounded;
    grounded.kind = BlockKind::If;
    grounded.pick = static_cast<int>(BlockTest::OnGround);
    Block setGround;
    setGround.kind = BlockKind::SetProp;
    setGround.name = "grounded";
    setGround.num = 1.f;
    Block refill;
    refill.kind = BlockKind::SetProp;
    refill.name = "jumps";
    refill.num = 1.f;
    grounded.inner.push_back(setGround);
    grounded.inner.push_back(refill);
    every.body.push_back(grounded);
    Script button;
    button.hat.kind = BlockKind::WhenButton1;
    Block inAir;
    inAir.kind = BlockKind::If;
    inAir.pick = static_cast<int>(BlockTest::Prop);
    inAir.name = "grounded";
    inAir.num = 1.f;
    inAir.num2 = 1.f;
    Block hasJump;
    hasJump.kind = BlockKind::If;
    hasJump.pick = static_cast<int>(BlockTest::Prop);
    hasJump.name = "jumps";
    hasJump.num2 = 0.f;
    Block jump;
    jump.kind = BlockKind::Jump;
    Block spend;
    spend.kind = BlockKind::ChangeProp;
    spend.name = "jumps";
    spend.num = -1.f;
    hasJump.inner.push_back(jump);
    hasJump.inner.push_back(spend);
    inAir.inner.push_back(hasJump);
    button.body.push_back(inAir);
    actor.scripts.clear();
    actor.scripts.push_back(std::move(every));
    actor.scripts.push_back(std::move(button));
    status_ = "Double jump replaced " + actor.name + "'s scripts. The ground jump is still built in.";
}

void Editor::givePatrol(ActorDef& actor) {
    Script every;
    every.hat.kind = BlockKind::EveryFrame;
    Block move;
    move.kind = BlockKind::Move;
    move.num = actor.behavior.speed > 1.f ? actor.behavior.speed : 60.f;
    Block wall;
    wall.kind = BlockKind::If;
    wall.pick = static_cast<int>(BlockTest::WallAhead);
    Block turn;
    turn.kind = BlockKind::Turn;
    wall.inner.push_back(turn);
    every.body.push_back(move);
    every.body.push_back(wall);
    actor.scripts.clear();
    actor.scripts.push_back(std::move(every));
    status_ = actor.name + " now patrols and turns at a wall.";
}

void Editor::giveOpenMessage(ActorDef& actor) {
    Script heard;
    heard.hat.kind = BlockKind::WhenMessage;
    heard.hat.name = "open";
    Block gone;
    gone.kind = BlockKind::Vanish;
    heard.body.push_back(gone);
    actor.scripts.clear();
    actor.scripts.push_back(std::move(heard));
    status_ = actor.name + " vanishes when it hears open.";
}

void Editor::giveHurtScore(ActorDef& actor) {
    Script hurt;
    hurt.hat.kind = BlockKind::WhenHurt;
    Block score;
    score.kind = BlockKind::AddScore;
    score.num = actor.behavior.score > 0 ? static_cast<float>(actor.behavior.score) : 100.f;
    Block gone;
    gone.kind = BlockKind::Vanish;
    hurt.body.push_back(score);
    hurt.body.push_back(gone);
    actor.scripts.clear();
    actor.scripts.push_back(std::move(hurt));
    status_ = actor.name + " vanishes and adds score when hurt.";
}

void Editor::dressStretch(bool recordUndo) {
    Stage* stage = currentStage();
    if (!stage) return;
    const int length = std::clamp(genLen_, 1, 80);
    const int x = std::clamp(genX_, 0, std::max(0, stage->width - 1));
    const int y = std::clamp(genY_, 0, std::max(0, stage->height - 1));
    const uint16_t solid = findTile(TileSolid, TileOneWay | TileLadder | TileHazard | TileWater);
    const uint16_t solidAny = solid != 0 ? solid : findTile(TileSolid, 0);
    const uint16_t lip = findTile(TileOneWay, TileHazard | TileLadder);
    uint16_t backdrop = 0;
    uint16_t metal = 0;
    const Tileset* set = tileset();
    if (set) {
        for (int i = 0; i < static_cast<int>(set->tiles.size()); ++i) {
            const uint32_t flags = set->tiles[static_cast<size_t>(i)].flags;
            const uint16_t id = static_cast<uint16_t>(i + 1);
            if (flags == 0 && backdrop == 0 && set->tileW > 0 && set->tileH > 0 && set->imgW > 0) {
                const int columns = std::max(1, set->columns);
                const int px = (i % columns) * set->tileW + set->tileW / 2;
                const int py = (i / columns) * set->tileH + set->tileH / 2;
                const size_t alpha = (static_cast<size_t>(py) * set->imgW + px) * 4 + 3;
                if (alpha < set->rgba.size() && set->rgba[alpha] > 200) backdrop = id;
            }
            if ((flags & TileSolid) &&
                !(flags & (TileOneWay | TileLadder | TileHazard | TileWater | TileDestructible | TileIce | TileCrumble)) &&
                id != solidAny)
                metal = id;
        }
        if (backdrop == 0) {
            for (int i = 0; i < static_cast<int>(set->tiles.size()); ++i) {
                if (set->tiles[static_cast<size_t>(i)].flags == 0) {
                    backdrop = static_cast<uint16_t>(i + 1);
                    break;
                }
            }
        }
    }
    if (recordUndo) pushUndo();
    auto paintBack = [&](int tx, int top) {
        if (backdrop == 0 || tx < 0 || tx >= stage->width) return;
        for (int yy = 0; yy <= top && yy < stage->height; ++yy) {
            if (stage->get(stage->back, tx, yy) == 0) stage->set(stage->back, tx, yy, backdrop);
        }
    };
    auto ground = [&](int tx, int top, int bottom) {
        if (tx < 0 || tx >= stage->width || top < 0 || top >= stage->height || solidAny == 0) return;
        if (lip != 0 && stage->get(stage->main, tx, top) == solidAny) stage->set(stage->main, tx, top, lip);
        const int stop = std::clamp(bottom, top, stage->height - 1);
        for (int yy = top + 1; yy <= stop; ++yy) {
            const uint16_t below = stage->get(stage->main, tx, yy);
            if (below == 0) stage->set(stage->main, tx, yy, solidAny);
            else if (below == solidAny || below == lip) continue;
            else break;
        }
        paintBack(tx, top);
    };
    const int bedrock = stage->height - 1;
    if (genShape_ == 1) {
        const bool pads = length >= 3;
        for (int i = 0; i < length; ++i) {
            if (pads && (i == 0 || i == length - 1)) ground(x + i, y, bedrock);
            else paintBack(x + i, y);
        }
    } else if (genShape_ == 2) {
        for (int i = 0; i < length; ++i) ground(x + i, std::max(0, y - i), y);
    } else if (genShape_ == 3) {
        const int top = std::max(0, y - length + 1);
        paintBack(x, y);
        const int ledgeY = std::min(y, top + 1);
        const int dir = x >= 5 ? -1 : 1;
        for (int i = 1; i <= 4; ++i) {
            const int tx = x + dir * i;
            if (lip != 0 && stage->get(stage->main, tx, ledgeY) == solidAny) stage->set(stage->main, tx, ledgeY, lip);
            paintBack(tx, ledgeY);
        }
    } else if (genShape_ == 4) {
        const uint16_t column = metal != 0 ? metal : solidAny;
        for (int i = 0; i < length; ++i) {
            const int ty = y + i;
            if (column != 0 && stage->get(stage->main, x, ty) == solidAny) stage->set(stage->main, x, ty, column);
            paintBack(x - 1, ty);
            paintBack(x, ty);
            paintBack(x + 1, ty);
        }
    } else {
        for (int i = 0; i < length; ++i) ground(x + i, y, bedrock);
    }
    if (recordUndo) status_ = "Dressed the stretch with ground, a top edge, and a backdrop.";
}

void Editor::copyChunk(int x, int y, int w, int h) {
    Stage* stage = currentStage();
    if (!stage) return;
    ensureLayers(*stage);
    const int x0 = std::clamp(x, 0, stage->width - 1);
    const int y0 = std::clamp(y, 0, stage->height - 1);
    const int x1 = std::clamp(x0 + std::max(1, w) - 1, x0, stage->width - 1);
    const int y1 = std::clamp(y0 + std::max(1, h) - 1, y0, stage->height - 1);
    Chunk chunk;
    chunk.w = x1 - x0 + 1;
    chunk.h = y1 - y0 + 1;
    chunk.main.reserve(static_cast<size_t>(chunk.w * chunk.h));
    chunk.back.reserve(chunk.main.capacity());
    chunk.front.reserve(chunk.main.capacity());
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            chunk.main.push_back(stage->get(stage->main, tx, ty));
            chunk.back.push_back(stage->get(stage->back, tx, ty));
            chunk.front.push_back(stage->get(stage->front, tx, ty));
        }
    }
    const Tileset* set = tileset();
    const float tileW = set && set->tileW > 0 ? static_cast<float>(set->tileW) : 16.f;
    const float tileH = set && set->tileH > 0 ? static_cast<float>(set->tileH) : 16.f;
    for (const auto& placement : stage->placements) {
        const int tx = static_cast<int>(std::floor(placement.x / tileW));
        const int ty = static_cast<int>(std::floor(placement.y / tileH));
        if (tx < x0 || ty < y0 || tx > x1 || ty > y1) continue;
        ClipActor actor;
        actor.dx = tx - x0;
        actor.dy = ty - y0;
        actor.ox = static_cast<int>(std::lround(placement.x - tx * tileW));
        actor.oy = static_cast<int>(std::lround(placement.y - ty * tileH));
        actor.actorId = placement.actorId;
        chunk.actors.push_back(std::move(actor));
    }
    chunk_ = std::move(chunk);
    popX_ = x0;
    popY_ = y0;
    popW_ = chunk_.w;
    popH_ = chunk_.h;
    status_ = "Copied " + std::to_string(chunk_.w) + "x" + std::to_string(chunk_.h) + " with " +
              std::to_string(chunk_.actors.size()) + " actors.";
}

void Editor::stampChunk(int x, int y) {
    Stage* stage = currentStage();
    if (!stage || chunk_.w <= 0 || chunk_.h <= 0) {
        status_ = "Copy an area first.";
        return;
    }
    ensureLayers(*stage);
    pushUndo();
    stampX_ = x;
    stampY_ = y;
    const Tileset* set = tileset();
    const float tileW = set && set->tileW > 0 ? static_cast<float>(set->tileW) : 16.f;
    const float tileH = set && set->tileH > 0 ? static_cast<float>(set->tileH) : 16.f;
    for (int dy = 0; dy < chunk_.h; ++dy) {
        for (int dx = 0; dx < chunk_.w; ++dx) {
            const size_t index = static_cast<size_t>(dy * chunk_.w + dx);
            stage->set(stage->main, x + dx, y + dy, chunk_.main[index]);
            stage->set(stage->back, x + dx, y + dy, chunk_.back[index]);
            stage->set(stage->front, x + dx, y + dy, chunk_.front[index]);
        }
    }
    int placed = 0;
    for (const auto& actor : chunk_.actors) {
        const int tx = x + actor.dx;
        const int ty = y + actor.dy;
        if (tx < 0 || ty < 0 || tx >= stage->width || ty >= stage->height) continue;
        const float px = tx * tileW + actor.ox;
        const float py = ty * tileH + actor.oy;
        bool blocked = false;
        for (const auto& placement : stage->placements) {
            if (placement.actorId == actor.actorId && std::abs(placement.x - px) < 2.f && std::abs(placement.y - py) < 2.f)
                blocked = true;
        }
        if (blocked) continue;
        Placement placement;
        placement.uid = project_.freshUid();
        placement.actorId = actor.actorId;
        placement.x = px;
        placement.y = py;
        stage->placements.push_back(std::move(placement));
        placed++;
    }
    status_ = "Stamped " + std::to_string(chunk_.w) + "x" + std::to_string(chunk_.h) + " and " + std::to_string(placed) +
              " actors.";
}

void Editor::stampChunkNext() {
    if (chunk_.w <= 0 || chunk_.h <= 0) {
        status_ = "Copy an area first.";
        return;
    }
    if (stageIndex_ + 1 >= static_cast<int>(project_.stages.size())) {
        status_ = "There is no next stage. Add one, then stamp.";
        return;
    }
    stageIndex_++;
    selectedPlacement_ = -1;
    stampChunk(stampX_, stampY_);
}

void Editor::drawBuildTab() {
    if (ImGui::Button("Build a stage", ImVec2(-1.f, 0))) buildStage();
    ImGui::TextWrapped("Builds a playable layout for this stage's game type. You can move or delete any of it.");
    if (ImGui::Button("Check this stage", ImVec2(-1.f, 0))) checkLevel();
    ImGui::Checkbox("Show collision", &showCollision_);
    if (showCollision_)
        ImGui::TextWrapped("Red is solid, green is one-way, yellow is a ladder, pink is a hazard, and blue is water.");
    ImGui::Separator();
    ImGui::TextUnformatted("Plugins");
    if (loadedPlugins().empty()) {
        ImGui::TextWrapped("Load a .ccplugin from the File menu. Make one with cabinet-plugin.");
    } else {
        for (const auto& plugin : loadedPlugins()) {
            ImGui::PushID(plugin.name.c_str());
            const std::string label = plugin.title.empty() ? plugin.name : plugin.title;
            ImGui::Text("%s", label.c_str());
            if (!plugin.summary.empty()) ImGui::TextWrapped("%s", plugin.summary.c_str());
            if (plugin.kind == PluginKind::Tool) {
                const bool armed = armedPlugin_ == plugin.name;
                if (ImGui::Button(armed ? "Stop tool" : "Use on the scene")) armedPlugin_ = armed ? std::string() : plugin.name;
            } else if (plugin.kind == PluginKind::Generator) {
                if (ImGui::Button("Run on this stage")) runPluginGenerator(plugin.name);
            } else {
                ImGui::TextWrapped("Open an actor's blocks. This plugin is in the Plugins list there.");
            }
            ImGui::PopID();
        }
    }
    if (levelChecked_) {
        ImGui::BeginChild("levelnotes", ImVec2(0.f, 120.f), true);
        for (const auto& note : levelNotes_) ImGui::BulletText("%s", note.c_str());
        ImGui::EndChild();
    }
    ImGui::Separator();
    ImGui::TextWrapped("Stretches and platforms paint the collision layer. You can move or delete anything afterward.");
    const char* shapes[] = {"Floor", "Pit", "Stairs", "Ladder to a ledge", "Shooter column"};
    ImGui::SetNextItemWidth(-1.f);
    ImGui::Combo("##shape", &genShape_, shapes, 5);
    ImGui::SetNextItemWidth(70.f);
    ImGui::InputInt("X", &genX_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.f);
    ImGui::InputInt("Y", &genY_);
    ImGui::SetNextItemWidth(70.f);
    ImGui::InputInt("Length", &genLen_);
    ImGui::Checkbox("Coins along it", &genCoins_);
    ImGui::Checkbox("Exit at the end", &genExit_);
    ImGui::Checkbox("Dress it", &genDress_);
    if (ImGui::Button("Apply stretch", ImVec2(-1.f, 0))) generateStretch(genX_, genY_, genLen_);
    if (ImGui::Button("Dress this stretch", ImVec2(-1.f, 0))) dressStretch(true);
    const char* stretchDrag = genDrag_ == 1 ? "Cancel stretch drag" : "Drag stretch on the map";
    if (ImGui::Button(stretchDrag, ImVec2(-1.f, 0))) {
        genDrag_ = genDrag_ == 1 ? 0 : 1;
        genDown_ = false;
        if (genDrag_ == 1) status_ = "Drag across the map, then release.";
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Populate");
    ImGui::SetNextItemWidth(70.f);
    ImGui::InputInt("Area X", &popX_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.f);
    ImGui::InputInt("Area Y", &popY_);
    ImGui::SetNextItemWidth(70.f);
    ImGui::InputInt("Width", &popW_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.f);
    ImGui::InputInt("Height", &popH_);
    ImGui::SetNextItemWidth(70.f);
    ImGui::InputInt("How many", &popCount_);
    popCount_ = std::clamp(popCount_, 1, 16);
    if (popActor_ < 0 || popActor_ >= static_cast<int>(project_.actors.size())) {
        popActor_ = 0;
        for (int i = 0; i < static_cast<int>(project_.actors.size()); ++i) {
            if (project_.actors[static_cast<size_t>(i)].id == "slime") popActor_ = i;
        }
    }
    const char* actorLabel = project_.actors.empty() ? "No actors" : project_.actors[static_cast<size_t>(popActor_)].name.c_str();
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::BeginCombo("##popactor", actorLabel)) {
        for (int i = 0; i < static_cast<int>(project_.actors.size()); ++i) {
            const ActorDef& actor = project_.actors[static_cast<size_t>(i)];
            if (actor.kind == ActorKind::Hero || actor.kind == ActorKind::Gun) continue;
            if (ImGui::Selectable(actor.name.c_str(), popActor_ == i)) popActor_ = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("On the floor", ImVec2(-1.f, 0))) populateFloor();
    if (ImGui::Button("Turret on a platform", ImVec2(-1.f, 0))) populateTurret();
    if (ImGui::Button("Key and door", ImVec2(-1.f, 0))) populateKeyDoor();
    const char* areaDrag = genDrag_ == 2 ? "Cancel area drag" : "Drag an area on the map";
    if (ImGui::Button(areaDrag, ImVec2(-1.f, 0))) {
        genDrag_ = genDrag_ == 2 ? 0 : 2;
        genDown_ = false;
        if (genDrag_ == 2) status_ = "Drag a rectangle, then release.";
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Copy");
    ImGui::TextWrapped("Copies tiles from all three layers and the actors inside the area.");
    if (ImGui::Button("Copy the area", ImVec2(-1.f, 0))) copyChunk(popX_, popY_, popW_, popH_);
    const char* copyDrag = genDrag_ == 3 ? "Cancel copy drag" : "Drag an area to copy";
    if (ImGui::Button(copyDrag, ImVec2(-1.f, 0))) {
        genDrag_ = genDrag_ == 3 ? 0 : 3;
        genDown_ = false;
        if (genDrag_ == 3) status_ = "Drag the chunk, then release.";
    }
    ImGui::SetNextItemWidth(70.f);
    ImGui::InputInt("Stamp X", &stampX_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.f);
    ImGui::InputInt("Stamp Y", &stampY_);
    if (ImGui::Button("Stamp here", ImVec2(-1.f, 0))) stampChunk(stampX_, stampY_);
    if (ImGui::Button("Stamp into the next stage", ImVec2(-1.f, 0))) stampChunkNext();
    const char* stampDrag = genDrag_ == 4 ? "Cancel stamp" : "Click the map to stamp";
    if (ImGui::Button(stampDrag, ImVec2(-1.f, 0))) {
        genDrag_ = genDrag_ == 4 ? 0 : 4;
        genDown_ = false;
        if (genDrag_ == 4) status_ = "Click where the chunk should land.";
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Ready-made scripts");
    if (selectedActor_ < 0 || selectedActor_ >= static_cast<int>(project_.actors.size())) {
        ImGui::TextWrapped("Select an actor first.");
    } else {
        ActorDef& actor = project_.actors[static_cast<size_t>(selectedActor_)];
        ImGui::TextWrapped("These replace the scripts on %s.", actor.name.c_str());
        if (ImGui::Button("Double jump", ImVec2(-1.f, 0))) giveDoubleJump(actor);
        if (ImGui::Button("Patrol and turn at a wall", ImVec2(-1.f, 0))) givePatrol(actor);
        if (ImGui::Button("Open when it hears open", ImVec2(-1.f, 0))) giveOpenMessage(actor);
        if (ImGui::Button("Vanish and score when hurt", ImVec2(-1.f, 0))) giveHurtScore(actor);
    }
}

void Editor::drawLeft() {
    const char* pages[] = {"Stages", "Tiles", "Actors", "Sprites", "Sounds", "Build", "Game"};
    leftPage_ = pageButtons(pages, 7, leftPage_);
    ImGui::Separator();
    if (leftPage_ == 0) {
            for (int i = 0; i < static_cast<int>(project_.stages.size()); ++i) {
                const std::string label = project_.stages[i].name + "##st" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), stageIndex_ == i)) {
                    stageIndex_ = i;
                    selectedPlacement_ = -1;
                }
            }
            if (ImGui::Button("Add")) addStage(false);
            ImGui::SameLine();
            if (ImGui::Button("Duplicate")) addStage(true);
            ImGui::SameLine();
            if (ImGui::Button("Delete")) deleteCurrentStage();
        }
        if (leftPage_ == 1) {
            const Tileset* set = tileset();
            const int tileW = set ? set->tileW : 16;
            const int tileH = set ? set->tileH : 16;
            const int count = set ? static_cast<int>(set->tiles.size()) : 0;
            if (ImGui::Selectable("Empty", selectedTile_ == 0)) selectedTile_ = 0;
            const int columns = set && set->columns > 0 ? set->columns : 8;
            for (int i = 1; i <= count; ++i) {
                ImGui::PushID(i);
                const bool on = selectedTile_ == i;
                const int cell = i - 1;
                const int col = cell % columns;
                const int row = cell / columns;
                if (tilesetTex_ && set && set->imgW > 0 && set->imgH > 0) {
                    const ImVec2 uv0(col * tileW / static_cast<float>(set->imgW), row * tileH / static_cast<float>(set->imgH));
                    const ImVec2 uv1((col * tileW + tileW) / static_cast<float>(set->imgW),
                                     (row * tileH + tileH) / static_cast<float>(set->imgH));
                    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.48f, 0.14f, 1.f));
                    if (ImGui::ImageButton(("tile" + std::to_string(i)).c_str(),
                                           static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(tilesetTex_)), ImVec2(36.f, 36.f), uv0,
                                           uv1, ImVec4(0.08f, 0.08f, 0.12f, 1.f))) {
                        selectedTile_ = i;
                        paintTarget_ = 1;
                        inspectPage_ = 1;
                    }
                    if (on) ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tile %d", i);
                    ImGui::SameLine();
                }
                if (ImGui::Selectable(("Tile " + std::to_string(i)).c_str(), on)) {
                    selectedTile_ = i;
                    paintTarget_ = 1;
                    inspectPage_ = 1;
                }
                ImGui::PopID();
            }
        }
        if (leftPage_ == 2) {
            for (int i = 0; i < static_cast<int>(project_.actors.size()); ++i) {
                const std::string label = project_.actors[i].name + "  " + kindName(project_.actors[i].kind);
                if (ImGui::Selectable((label + "##a" + std::to_string(i)).c_str(), selectedActor_ == i)) selectedActor_ = i;
            }
            if (ImGui::Button("Enemy")) addActor(ActorKind::Enemy);
            ImGui::SameLine();
            if (ImGui::Button("Item")) addActor(ActorKind::Item);
            ImGui::SameLine();
            if (ImGui::Button("Gun")) addActor(ActorKind::Gun);
            if (ImGui::Button("Check")) addActor(ActorKind::Check);
            ImGui::SameLine();
            if (ImGui::Button("Boss")) addActor(ActorKind::Boss);
        }
        if (leftPage_ == 3) {
            for (int i = 0; i < static_cast<int>(project_.sprites.size()); ++i) {
                if (ImGui::Selectable((project_.sprites[i].name + "##sp" + std::to_string(i)).c_str(), selectedSprite_ == i)) {
                    selectedSprite_ = i;
                    paintTarget_ = 0;
                }
            }
            if (ImGui::Button("New sprite")) {
                Sprite sprite;
                sprite.id = uniqueId(project_, "sprite");
                sprite.name = "Sprite";
                sprite.frameW = 16;
                sprite.frameH = 16;
                sprite.frames = 1;
                sprite.imgW = 16;
                sprite.imgH = 16;
                sprite.rgba.assign(16 * 16 * 4, 0);
                project_.sprites.push_back(sprite);
                selectedSprite_ = static_cast<int>(project_.sprites.size()) - 1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Import PNG")) openFilePick(FilePick::ImportPng);
        }
        if (leftPage_ == 4) {
            for (int i = 0; i < static_cast<int>(project_.sounds.size()); ++i) {
                const std::string label = project_.sounds[i].name + (project_.sounds[i].music ? "  (music)" : "");
                if (ImGui::Selectable((label + "##so" + std::to_string(i)).c_str(), selectedSound_ == i)) selectedSound_ = i;
            }
            if (selectedSound_ >= 0 && ImGui::Button("Preview")) {
                audio_.open();
                audio_.play(&project_.sounds[selectedSound_], 0.8f, project_.sounds[selectedSound_].music);
            }
            ImGui::SameLine();
            if (ImGui::Button("Import WAV")) openFilePick(FilePick::ImportWav);
            ImGui::Separator();
            ImGui::TextUnformatted("Sound maker");
            editString("Name", soundName_);
            const char* styles[] = {"Square beep", "Soft tone", "Slide up", "Slide down", "Noise", "Two notes", "Arpeggio"};
            ImGui::SetNextItemWidth(-1.f);
            ImGui::Combo("Style", &soundStyle_, styles, 7);
            ImGui::SliderInt("Note", &soundNote_, 36, 96);
            ImGui::SliderFloat("Length", &soundSeconds_, 0.05f, 2.f, "%.2f s");
            ImGui::Checkbox("Music loop", &soundMusic_);
            if (ImGui::Button("Make sound", ImVec2(-1.f, 0))) {
                Sound made = synthMade(uniqueId(project_, soundMusic_ ? "tune" : "blip"), soundName_.empty() ? "Blip" : soundName_,
                                       soundStyle_, soundNote_, soundSeconds_, soundMusic_);
                project_.sounds.push_back(std::move(made));
                selectedSound_ = static_cast<int>(project_.sounds.size()) - 1;
                audio_.open();
                audio_.play(&project_.sounds[selectedSound_], 0.8f, project_.sounds[selectedSound_].music);
                status_ = "Added " + project_.sounds[selectedSound_].name;
            }
            if (selectedSound_ >= 0 && selectedSound_ < static_cast<int>(project_.sounds.size()) && currentStage()) {
                if (ImGui::Button("Use as this stage's music", ImVec2(-1.f, 0))) {
                    Sound& sound = project_.sounds[selectedSound_];
                    sound.music = true;
                    currentStage()->musicId = sound.id;
                    status_ = "Stage music is " + sound.name;
                }
            }
            auto replaceSound = [&](Sound made) {
                for (int i = 0; i < static_cast<int>(project_.sounds.size()); ++i) {
                    if (project_.sounds[static_cast<size_t>(i)].id == made.id) {
                        project_.sounds[static_cast<size_t>(i)] = std::move(made);
                        selectedSound_ = i;
                        return;
                    }
                }
                project_.sounds.push_back(std::move(made));
                selectedSound_ = static_cast<int>(project_.sounds.size()) - 1;
            };
            if (ImGui::Button("Jump", ImVec2(70.f, 0))) replaceSound(synthJump());
            ImGui::SameLine();
            if (ImGui::Button("Shoot", ImVec2(70.f, 0))) replaceSound(synthShoot());
            ImGui::SameLine();
            if (ImGui::Button("Coin", ImVec2(70.f, 0))) replaceSound(synthCoin());
            ImGui::TextWrapped("Jump, Shoot, and Coin replace the sounds the game already plays. Make sound adds a new one.");
        }
        if (leftPage_ == 5) {
            drawBuildTab();
        }
        if (leftPage_ == 6) {
            editString("Title", project_.settings.title);
            editString("Author", project_.settings.author);
            editInt("View width", project_.settings.viewW);
            editInt("View height", project_.settings.viewH);
            editInt("Window scale", project_.settings.scale);
            editInt("Lives", project_.settings.lives);
            editInt("Health", project_.settings.maxHealth);
            project_.settings.viewW = std::clamp(project_.settings.viewW, 160, 640);
            project_.settings.viewH = std::clamp(project_.settings.viewH, 120, 480);
            project_.settings.scale = std::clamp(project_.settings.scale, 1, 6);
            ImGui::Checkbox("CRT scanlines", &project_.settings.crt);
            ImGui::Checkbox("Screen shake", &project_.settings.screenShake);
            ImGui::Checkbox("Free play", &project_.settings.freePlay);
            ImGui::TextWrapped(
                "Free play begins when Start is pressed. Turn it off for a cabinet. The coin switch is the 5 key and adds a credit. Start begins a game only while a credit is waiting.");
            editString("Start stage id", project_.settings.startStage);
            title_ = "CabinetCore — " + project_.settings.title;
        }
}

void Editor::drawCanvas() {
    hoverHint_.clear();
    if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) zoomWheel(ImGui::GetIO().MouseWheel);
    Stage* stage = currentStage();
    if (!stage || !mapTex_) {
        ImGui::TextUnformatted("No stage");
        return;
    }
    const ImVec2 size(static_cast<float>(mapTexW_ * zoom_), static_cast<float>(mapTexH_ * zoom_));
    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(mapTex_)), size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 origin = ImGui::GetItemRectMin();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float localX = (mouse.x - origin.x) / static_cast<float>(zoom_);
    const float localY = (mouse.y - origin.y) / static_cast<float>(zoom_);
    const Tileset* set = tileset();
    const int tileW = set ? set->tileW : 16;
    const int tileH = set ? set->tileH : 16;
    const int tx = static_cast<int>(std::floor(localX / tileW));
    const int ty = static_cast<int>(std::floor(localY / tileH));
    if (hovered) {
        if (genDrag_ == 1) status_ = "Drag the stretch, then release.";
        else if (genDrag_ == 2) status_ = "Drag the area, then release.";
        else if (genDrag_ == 3) status_ = "Drag the chunk to copy, then release.";
        else if (genDrag_ == 4) status_ = "Click where the chunk should land.";
        else hoverHint_ = "Tile " + std::to_string(tx) + ", " + std::to_string(ty);
        const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool release = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        const int hit = click ? placementAt(localX, localY) : -1;
        if (click && hit >= 0) {
            tool_ = 0;
            draggingActor_ = false;
            selectedPlacement_ = hit;
            const std::string& id = stage->placements[static_cast<size_t>(hit)].actorId;
            for (int i = 0; i < static_cast<int>(project_.actors.size()); ++i) {
                if (project_.actors[static_cast<size_t>(i)].id == id) selectedActor_ = i;
            }
            const ActorDef* actor = project_.findActor(id);
            status_ = std::string("Selected ") + (actor ? actor->name : id);
            inspectPage_ = 2;
        } else if (genDrag_ != 0) {
            if (click) {
                genDown_ = true;
                genX0_ = genX1_ = tx;
                genY0_ = genY1_ = ty;
                draggingActor_ = false;
            } else if (down && genDown_) {
                genX1_ = tx;
                genY1_ = ty;
            }
            if (genDown_) {
                const int x0 = std::min(genX0_, genX1_);
                const int y0 = std::min(genY0_, genY1_);
                const int x1 = std::max(genX0_, genX1_);
                const int y1 = std::max(genY0_, genY1_);
                const ImVec2 a(origin.x + x0 * tileW * zoom_, origin.y + y0 * tileH * zoom_);
                const ImVec2 b(origin.x + (x1 + 1) * tileW * zoom_, origin.y + (y1 + 1) * tileH * zoom_);
                ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32(255, 210, 60, 255), 0.f, 0, 2.f);
            }
        } else if (!armedPlugin_.empty() && click) {
            runPluginTool(tx, ty);
        } else {
            if (click) {
                pushUndo();
                stroking_ = true;
                draggingActor_ = false;
            }
            if (click || (down && (tool_ == 1 || tool_ == 2))) applyTool(tx, ty, click, true);
            if (release && tool_ == 4) applyTool(tx, ty, false, false);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            selectedTile_ = stage->get(activeLayer(), tx, ty);
            status_ = "Picked tile " + std::to_string(selectedTile_);
            inspectPage_ = 1;
        }
        if (tool_ == 0 && selectedPlacement_ >= 0 && down && !click &&
            selectedPlacement_ < static_cast<int>(stage->placements.size())) {
            if (!draggingActor_) {
                pushUndo();
                draggingActor_ = true;
            }
            Placement& placement = stage->placements[selectedPlacement_];
            placement.x = snapCoord(localX - tileW * 0.5f, tileW);
            placement.y = snapCoord(localY - tileH * 0.5f, tileH);
        }
        if (release) {
            stroking_ = false;
            draggingActor_ = false;
        }
        if (!armedPlugin_.empty()) hoverHint_ += "   " + armedPlugin_;
    }
    if (genDown_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) finishGeneratorDrag();
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedPlacement_ >= 0 &&
        selectedPlacement_ < static_cast<int>(stage->placements.size())) {
        pushUndo();
        stage->placements.erase(stage->placements.begin() + selectedPlacement_);
        selectedPlacement_ = -1;
    }
}

void Editor::drawStageSettings(Stage& stage) {
    editString("Name", stage.name);
    editString("Id", stage.id);
    editString("Music id", stage.musicId);
    editString("Message", stage.message);
    ImGui::Separator();
    uint32_t physics = static_cast<uint32_t>(stage.physics);
    uint32_t scroll = static_cast<uint32_t>(stage.scroll);
    uint32_t complete = static_cast<uint32_t>(stage.complete);
    if (editEnum("Physics", physics, kPhysics, 6)) stage.physics = static_cast<PhysicsMode>(physics);
    if (editEnum("Scroll", scroll, kScroll, 6)) stage.scroll = static_cast<ScrollMode>(scroll);
    if (editEnum("Complete", complete, kComplete, 4)) stage.complete = static_cast<CompleteMode>(complete);
    ImGui::Checkbox("Intermission", &stage.intermission);
    ImGui::Separator();
    editFloat("Auto scroll", stage.autoScroll);
    editFloat("Gravity", stage.gravity);
    editFloat("Jump", stage.jumpVel);
    editFloat("Player speed", stage.playerSpeed);
    static Stage* synced = nullptr;
    static int newW = 20;
    static int newH = 15;
    if (synced != &stage) {
        synced = &stage;
        newW = stage.width;
        newH = stage.height;
    }
    ImGui::Separator();
    editInt("Width", newW);
    editInt("Height", newH);
    if (ImGui::Button("Resize stage", ImVec2(-1.f, 0))) resizeCurrentStage(newW, newH);
}

void Editor::drawActorEditor(ActorDef& actor) {
    editString("Name", actor.name);
    ImGui::Text("Id: %s", actor.id.c_str());
    uint32_t kind = static_cast<uint32_t>(actor.kind);
    uint32_t pickup = static_cast<uint32_t>(actor.pickup);
    uint32_t pattern = static_cast<uint32_t>(actor.behavior.pattern);
    uint32_t aim = static_cast<uint32_t>(actor.behavior.aim);
    if (editEnum("Kind", kind, kKinds, 7)) actor.kind = static_cast<ActorKind>(kind);
    if (editEnum("Behavior", pattern, kPatterns, 14)) {
        actor.behavior.pattern = static_cast<AiPattern>(pattern);
        if (actor.behavior.pattern == AiPattern::Platform) {
            actor.behavior.gravity = false;
            actor.behavior.touchDamage = 0;
            if (actor.behavior.amplitude < 8.f) actor.behavior.amplitude = 64.f;
            if (actor.behavior.speed < 8.f) actor.behavior.speed = 40.f;
        }
    }
    if (editEnum("Aim", aim, kAims, 5)) actor.behavior.aim = static_cast<AimMode>(aim);
    if (editEnum("Pickup", pickup, kPickups, 9)) actor.pickup = static_cast<Pickup>(pickup);
    if (ImGui::BeginCombo("Sprite", actor.spriteId.c_str())) {
        for (const auto& sprite : project_.sprites) {
            if (ImGui::Selectable(sprite.id.c_str(), sprite.id == actor.spriteId)) actor.spriteId = sprite.id;
        }
        ImGui::EndCombo();
    }
    editString("Projectile id", actor.projectileId);
    editString("Drop id", actor.dropActorId);
    editFloat("Hit width", actor.hitW);
    editFloat("Hit height", actor.hitH);
    editFloat("Speed", actor.behavior.speed);
    editFloat("Fire every", actor.behavior.fireInterval);
    editFloat("Shot speed", actor.behavior.shotSpeed);
    editFloat("Spread", actor.behavior.spread);
    editInt("Burst", actor.behavior.burst);
    editInt("HP", actor.behavior.hp);
    editInt("Touch damage", actor.behavior.touchDamage);
    ImGui::Checkbox("Stomp from above", &actor.behavior.stomp);
    if (actor.behavior.pattern == AiPattern::Platform) {
        editFloat("Travel distance", actor.behavior.amplitude);
        ImGui::TextWrapped("Facing rides left and right from where you placed it. Up or Down rides vertically. Speed is how fast it moves.");
    }
    if (actor.pickup == Pickup::Special)
        ImGui::TextWrapped(
            "Button 2 still fires the normal shot. Hold it to spend one special. Amount is how many you pick up. A projectile fires that shot; leave it empty to clear the screen. Burst above 1 spreads.");
    editInt("Score", actor.behavior.score);
    editInt("Pickup amount", actor.pickupAmount);
    ImGui::Checkbox("Gravity", &actor.behavior.gravity);
    ImGui::Checkbox("Turn at edges", &actor.behavior.turnAtEdge);
    ImGui::Checkbox("Needs key", &actor.needsKey);
    ImGui::Checkbox("Checkpoint", &actor.checkpoint);
    ImGui::Separator();
    const char* blockLabel = actor.kind == ActorKind::Hero ? "Hero properties and blocks" : "Custom actor blocks";
    if (ImGui::Button(blockLabel, ImVec2(-1, 0))) {
        blocksOpen_ = true;
        blockActor_ = selectedActor_;
        blockScript_ = 0;
        blockPath_.clear();
        blockEdit_ = 0;
    }
    ImGui::Text("%d properties, %d scripts", static_cast<int>(actor.props.size()), static_cast<int>(actor.scripts.size()));
}

void Editor::drawTileFlags() {
    Tileset* set = tileset();
    if (!set || selectedTile_ <= 0 || selectedTile_ > static_cast<int>(set->tiles.size())) {
        ImGui::TextUnformatted("Select a tile to edit collision.");
        return;
    }
    TileDef& tile = set->tiles[selectedTile_ - 1];
    auto flag = [&](const char* label, uint32_t bit) {
        bool on = (tile.flags & bit) != 0;
        if (ImGui::Checkbox(label, &on)) {
            if (on) tile.flags |= bit;
            else tile.flags &= ~bit;
        }
    };
    ImGui::Text("Tile %d", selectedTile_);
    const float mid = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x * 0.5f;
    auto pair = [&](const char* left, uint32_t leftBit, const char* right, uint32_t rightBit) {
        flag(left, leftBit);
        ImGui::SameLine(mid);
        flag(right, rightBit);
    };
    pair("Solid", TileSolid, "One-way", TileOneWay);
    pair("Ladder", TileLadder, "Hazard", TileHazard);
    pair("Water", TileWater, "Ice", TileIce);
    pair("Destructible", TileDestructible, "Crumble", TileCrumble);
    editInt("Anim frames", tile.frames);
    editFloat("Anim fps", tile.fps);
}

void Editor::drawInspector() {
    Stage* stage = currentStage();
    if (!stage) return;
    const char* pages[] = {"Stage", "Tile", "Actor"};
    inspectPage_ = pageButtons(pages, 3, inspectPage_);
    ImGui::Separator();
    if (inspectPage_ == 0) {
        ImGui::PushID("stage");
        drawStageSettings(*stage);
        ImGui::PopID();
        return;
    }
    if (inspectPage_ == 1) {
        ImGui::PushID("tile");
        drawTileFlags();
        ImGui::PopID();
        return;
    }
    if (selectedPlacement_ >= 0 && selectedPlacement_ < static_cast<int>(stage->placements.size())) {
        ImGui::PushID("placed");
        Placement& placement = stage->placements[selectedPlacement_];
        const Tileset* set = tileset();
        const int tileW = set && set->tileW > 0 ? set->tileW : 16;
        const int tileH = set && set->tileH > 0 ? set->tileH : 16;
        float x = placement.x;
        float y = placement.y;
        if (editFloat("X", x)) placement.x = snapCoord(x, tileW);
        if (editFloat("Y", y)) placement.y = snapCoord(y, tileH);
        beginField("Actor");
        if (ImGui::BeginCombo("##placed-actor", placement.actorId.c_str())) {
            for (const auto& actor : project_.actors) {
                if (ImGui::Selectable((actor.name + "##" + actor.id).c_str(), actor.id == placement.actorId))
                    placement.actorId = actor.id;
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Remove from stage", ImVec2(-1.f, 0))) {
            pushUndo();
            stage->placements.erase(stage->placements.begin() + selectedPlacement_);
            selectedPlacement_ = -1;
        }
        ImGui::PopID();
        ImGui::Separator();
    }
    if (selectedActor_ >= 0 && selectedActor_ < static_cast<int>(project_.actors.size())) {
        ImGui::PushID("actor");
        drawActorEditor(project_.actors[selectedActor_]);
        ImGui::PopID();
    }
}

void Editor::normalizeSprite(Sprite& sprite) {
    sprite.frameW = std::clamp(sprite.frameW, 1, 128);
    sprite.frameH = std::clamp(sprite.frameH, 1, 128);
    sprite.frames = std::clamp(sprite.frames, 1, 32);
    const int imgW = sprite.frameW * sprite.frames;
    const int imgH = sprite.frameH;
    if (sprite.imgW == imgW && sprite.imgH == imgH && static_cast<int>(sprite.rgba.size()) == imgW * imgH * 4) return;
    std::vector<uint8_t> next(static_cast<size_t>(imgW) * imgH * 4, 0);
    const int copyW = std::min(imgW, sprite.imgW);
    const int copyH = std::min(imgH, sprite.imgH);
    for (int y = 0; y < copyH; ++y) {
        if (sprite.imgW <= 0) break;
        std::memcpy(next.data() + static_cast<size_t>(y) * imgW * 4, sprite.rgba.data() + static_cast<size_t>(y) * sprite.imgW * 4,
                    static_cast<size_t>(copyW) * 4);
    }
    sprite.rgba.swap(next);
    sprite.imgW = imgW;
    sprite.imgH = imgH;
}

size_t sheetPixel(int imgW, int ox, int oy, int x, int y) {
    return (static_cast<size_t>(oy + y) * imgW + ox + x) * 4;
}

void Editor::plotSheet(std::vector<uint8_t>& rgba, int imgW, int ox, int oy, int w, int h, int x, int y, bool erase) {
    if (x < 0 || y < 0 || x >= w || y >= h || imgW <= 0) return;
    const size_t i = sheetPixel(imgW, ox, oy, x, y);
    if (i + 3 >= rgba.size()) return;
    if (erase || colorIndex_ < 0) {
        rgba[i] = rgba[i + 1] = rgba[i + 2] = rgba[i + 3] = 0;
    } else {
        rgba[i] = paintR_;
        rgba[i + 1] = paintG_;
        rgba[i + 2] = paintB_;
        rgba[i + 3] = 255;
    }
}

void Editor::floodSheet(std::vector<uint8_t>& rgba, int imgW, int ox, int oy, int w, int h, int x, int y, bool erase) {
    if (x < 0 || y < 0 || x >= w || y >= h || imgW <= 0) return;
    const size_t start = sheetPixel(imgW, ox, oy, x, y);
    if (start + 3 >= rgba.size()) return;
    const uint8_t tr = rgba[start];
    const uint8_t tg = rgba[start + 1];
    const uint8_t tb = rgba[start + 2];
    const uint8_t ta = rgba[start + 3];
    const uint8_t nr = erase || colorIndex_ < 0 ? 0 : paintR_;
    const uint8_t ng = erase || colorIndex_ < 0 ? 0 : paintG_;
    const uint8_t nb = erase || colorIndex_ < 0 ? 0 : paintB_;
    const uint8_t na = erase || colorIndex_ < 0 ? 0 : 255;
    if (tr == nr && tg == ng && tb == nb && ta == na) return;
    std::vector<std::pair<int, int>> stack;
    stack.push_back({x, y});
    int guard = w * h * 8 + 8;
    while (!stack.empty() && guard-- > 0) {
        const int cx = stack.back().first;
        const int cy = stack.back().second;
        stack.pop_back();
        if (cx < 0 || cy < 0 || cx >= w || cy >= h) continue;
        const size_t i = sheetPixel(imgW, ox, oy, cx, cy);
        if (i + 3 >= rgba.size()) continue;
        if (rgba[i] != tr || rgba[i + 1] != tg || rgba[i + 2] != tb || rgba[i + 3] != ta) continue;
        plotSheet(rgba, imgW, ox, oy, w, h, cx, cy, erase);
        stack.push_back({cx + 1, cy});
        stack.push_back({cx - 1, cy});
        stack.push_back({cx, cy + 1});
        stack.push_back({cx, cy - 1});
    }
}

void Editor::strokeSheet(std::vector<uint8_t>& rgba, int imgW, int ox, int oy, int w, int h, int x0, int y0, int x1, int y1,
                         bool erase) {
    int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plotSheet(rgba, imgW, ox, oy, w, h, x0, y0, erase);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Editor::pickSheet(std::vector<uint8_t>& rgba, int imgW, int ox, int oy, int w, int h, int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h || imgW <= 0) return;
    const size_t i = sheetPixel(imgW, ox, oy, x, y);
    if (i + 3 >= rgba.size()) return;
    if (rgba[i + 3] == 0) {
        colorIndex_ = -1;
        return;
    }
    colorIndex_ = 16;
    paintR_ = rgba[i];
    paintG_ = rgba[i + 1];
    paintB_ = rgba[i + 2];
}

void Editor::plotSprite(Sprite& sprite, int frame, int x, int y, bool erase) {
    if (frame < 0 || frame >= sprite.frames) return;
    plotSheet(sprite.rgba, sprite.imgW, frame * sprite.frameW, 0, sprite.frameW, sprite.frameH, x, y, erase);
}

void Editor::floodSprite(Sprite& sprite, int frame, int x, int y, bool erase) {
    if (frame < 0 || frame >= sprite.frames) return;
    floodSheet(sprite.rgba, sprite.imgW, frame * sprite.frameW, 0, sprite.frameW, sprite.frameH, x, y, erase);
}

void Editor::strokeSprite(Sprite& sprite, int frame, int x0, int y0, int x1, int y1, bool erase) {
    if (frame < 0 || frame >= sprite.frames) return;
    strokeSheet(sprite.rgba, sprite.imgW, frame * sprite.frameW, 0, sprite.frameW, sprite.frameH, x0, y0, x1, y1, erase);
}

void Editor::copySpriteFrame(Sprite& sprite, int frame) {
    if (frame <= 0 || frame >= sprite.frames || sprite.frameW <= 0 || sprite.imgW <= 0) return;
    const int previous = (frame - 1) * sprite.frameW;
    const int current = frame * sprite.frameW;
    for (int y = 0; y < sprite.frameH; ++y) {
        const size_t row = static_cast<size_t>(y) * sprite.imgW;
        if ((row + current + sprite.frameW) * 4 > sprite.rgba.size()) return;
        std::memcpy(sprite.rgba.data() + (row + current) * 4, sprite.rgba.data() + (row + previous) * 4,
                    static_cast<size_t>(sprite.frameW) * 4);
    }
}

void Editor::pickSprite(Sprite& sprite, int frame, int x, int y) {
    if (frame < 0 || frame >= sprite.frames) return;
    pickSheet(sprite.rgba, sprite.imgW, frame * sprite.frameW, 0, sprite.frameW, sprite.frameH, x, y);
}

void Editor::buildStage() {
    Stage* stage = currentStage();
    if (!stage) return;
    if (stage->intermission) {
        status_ = "This stage is a message screen.";
        return;
    }
    const auto pickHero = [&]() -> const ActorDef* {
        const char* prefer = "runner";
        if (stage->physics == PhysicsMode::ShooterVert || stage->physics == PhysicsMode::ShooterHorz) prefer = "ship";
        if (const ActorDef* named = project_.findActor(prefer))
            if (named->kind == ActorKind::Hero) return named;
        for (const auto& actor : project_.actors)
            if (actor.kind == ActorKind::Hero) return &actor;
        return nullptr;
    };
    const ActorDef* hero = pickHero();
    if (!hero) {
        status_ = "Add a hero first.";
        return;
    }
    const uint16_t solid = findTile(TileSolid, TileOneWay | TileLadder | TileHazard | TileWater);
    const uint16_t solidAny = solid != 0 ? solid : findTile(TileSolid, 0);
    if (solidAny == 0) {
        status_ = "This tileset has no solid tile.";
        return;
    }
    const uint16_t hazard = findTile(TileHazard, TileSolid);
    const uint16_t ladderTile = findTile(TileLadder, TileSolid | TileHazard);
    const ActorDef* coin = project_.findActor("coin");
    if (!coin) {
        for (const auto& actor : project_.actors)
            if (actor.pickup == Pickup::Coin) {
                coin = &actor;
                break;
            }
    }
    const ActorDef* door = project_.findActor("door");
    if (!door) {
        for (const auto& actor : project_.actors)
            if (actor.kind == ActorKind::Check && !actor.checkpoint) {
                door = &actor;
                break;
            }
    }
    const ActorDef* enemy = project_.findActor("slime");
    if (!enemy) {
        for (const auto& actor : project_.actors)
            if (actor.kind == ActorKind::Enemy) {
                enemy = &actor;
                break;
            }
    }
    const Tileset* set = tileset();
    const float tileWf = set && set->tileW > 0 ? static_cast<float>(set->tileW) : 16.f;
    const float tileHf = set && set->tileH > 0 ? static_cast<float>(set->tileH) : 16.f;
    const int savedShape = genShape_;
    const int savedX = genX_;
    const int savedY = genY_;
    const int savedLen = genLen_;
    const bool savedCoins = genCoins_;
    const bool savedExit = genExit_;
    const bool savedDress = genDress_;

    pushUndo();
    undoSuspend_ = true;
    undoUsed_ = true;
    for (int y = 0; y < stage->height; ++y) {
        for (int x = 0; x < stage->width; ++x) {
            stage->set(stage->main, x, y, 0);
            stage->set(stage->back, x, y, 0);
        }
    }
    stage->placements.clear();

    auto dropUnsupported = [&]() {
        stage->placements.erase(std::remove_if(stage->placements.begin(), stage->placements.end(),
                                               [&](const Placement& placement) {
                                                   const int tx = static_cast<int>(placement.x / tileWf);
                                                   const int ty = static_cast<int>(placement.y / tileHf);
                                                   if (tx < 0 || ty < 0 || tx >= stage->width || ty + 1 >= stage->height) return false;
                                                   return !standable(*stage, tx, ty + 1);
                                               }),
                                stage->placements.end());
    };

    const PhysicsMode mode = stage->physics;
    if (mode == PhysicsMode::Maze) {
        for (int y = 0; y < stage->height; ++y) {
            for (int x = 0; x < stage->width; ++x) {
                const bool border = x == 0 || y == 0 || x == stage->width - 1 || y == stage->height - 1;
                const bool pillar = (x % 2 == 0) && (y % 2 == 0);
                if (border || pillar) stage->set(stage->main, x, y, solidAny);
            }
        }
        const int hx = stage->width > 2 ? 1 : 0;
        const int hy = stage->height > 2 ? 1 : 0;
        placeActor(*stage, hero, hx, hy);
        if (coin) {
            for (int y = 1; y < stage->height - 1; ++y)
                for (int x = 1; x < stage->width - 1; ++x)
                    if (stage->get(stage->main, x, y) == 0 && ((x + y) % 4) == 0) placeActor(*stage, coin, x, y);
        }
        stage->complete = CompleteMode::CollectAll;
    } else if (mode == PhysicsMode::TopDown) {
        for (int y = 0; y < stage->height; ++y) {
            for (int x = 0; x < stage->width; ++x) {
                const bool border = x == 0 || y == 0 || x == stage->width - 1 || y == stage->height - 1;
                const bool block = x > 2 && y > 2 && x < stage->width - 3 && y < stage->height - 3 && (x % 5) == 0 && (y % 4) == 0;
                if (border || block) stage->set(stage->main, x, y, solidAny);
            }
        }
        placeActor(*stage, hero, stage->width > 2 ? 2 : 0, stage->height > 2 ? 2 : 0);
        const int dx = std::max(1, stage->width - 2);
        const int dy = std::max(1, stage->height - 2);
        stage->set(stage->main, dx, dy, 0);
        if (door) placeActor(*stage, door, dx, dy);
        if (coin) {
            placeActor(*stage, coin, std::min(3, stage->width - 1), stage->height / 2);
            placeActor(*stage, coin, stage->width / 2, std::min(3, stage->height - 1));
        }
        if (enemy) placeActor(*stage, enemy, stage->width / 2, stage->height / 2);
        stage->complete = CompleteMode::TouchExit;
    } else if (mode == PhysicsMode::ShooterVert || mode == PhysicsMode::ShooterHorz) {
        const int columns = std::clamp(stage->width / 8, 2, 6);
        genShape_ = 4;
        genCoins_ = false;
        genExit_ = false;
        genDress_ = false;
        for (int i = 1; i <= columns; ++i) {
            const int x = std::clamp(i * stage->width / (columns + 1), 1, std::max(1, stage->width - 2));
            const int len = std::clamp(stage->height / 5, 2, 8);
            const int y = mode == PhysicsMode::ShooterVert ? std::max(1, stage->height / 5) : std::max(1, stage->height / 4);
            generateStretch(x, y, len);
            dressStretch(false);
        }
        auto openCell = [&](int x, int y) {
            return x > 0 && y > 0 && x < stage->width - 1 && y < stage->height - 1 && stage->get(stage->main, x, y) == 0;
        };
        if (mode == PhysicsMode::ShooterVert) {
            const int hx = stage->width / 2;
            const int hy = std::max(1, stage->height - 3);
            if (openCell(hx, hy)) placeActor(*stage, hero, hx, hy);
            else placeActor(*stage, hero, 1, hy);
        } else {
            const int hy = stage->height / 2;
            if (openCell(2, hy)) placeActor(*stage, hero, 2, hy);
            else placeActor(*stage, hero, 1, 1);
        }
        if (enemy) {
            if (openCell(stage->width / 3, 2)) placeActor(*stage, enemy, stage->width / 3, 2);
            if (openCell((2 * stage->width) / 3, stage->height / 3))
                placeActor(*stage, enemy, (2 * stage->width) / 3, stage->height / 3);
        }
        stage->complete = CompleteMode::ReachEnd;
    } else {
        const int floorY = std::max(2, stage->height - 3);
        for (int x = 0; x < stage->width; ++x) {
            stage->set(stage->main, x, floorY, solidAny);
            for (int y = floorY + 1; y < stage->height; ++y) stage->set(stage->main, x, y, solidAny);
        }
        genShape_ = 0;
        genCoins_ = false;
        genExit_ = false;
        genDress_ = false;
        for (int x = 0; x < stage->width; x += 80) {
            genX_ = x;
            genY_ = floorY;
            genLen_ = std::min(80, stage->width - x);
            dressStretch(false);
        }
        if (stage->width >= 16 && floorY >= 4) {
            genShape_ = 2;
            genCoins_ = coin != nullptr;
            genExit_ = false;
            const int len = std::min(4, floorY);
            generateStretch(6, floorY, len);
            dressStretch(false);
        }
        if (ladderTile != 0 && stage->width >= 22 && floorY >= 4) {
            const int pitA = stage->width / 3;
            const int pitB = (2 * stage->width) / 3;
            int ladderX = -1;
            for (int x = stage->width - 6; x >= 8; --x) {
                const bool inPit = (x >= pitA && x < pitA + 3) || (stage->width >= 24 && x >= pitB && x < pitB + 3);
                bool ledgeClear = true;
                for (int i = 1; i <= 4; ++i) {
                    const int tx = x - i;
                    if (tx < 1 || (tx >= pitA && tx < pitA + 3) || (stage->width >= 24 && tx >= pitB && tx < pitB + 3)) ledgeClear = false;
                }
                if (!inPit && ledgeClear) {
                    ladderX = x;
                    break;
                }
            }
            if (ladderX >= 0) {
                genShape_ = 3;
                genCoins_ = coin != nullptr;
                genExit_ = false;
                generateStretch(ladderX, floorY, std::min(5, floorY));
                dressStretch(false);
            }
        }
        if (stage->width >= 18) {
            auto carve = [&](int start) {
                for (int i = 0; i < 3; ++i) {
                    const int x = start + i;
                    if (x < 4 || x > stage->width - 5) continue;
                    for (int y = floorY; y < stage->height - 1; ++y) stage->set(stage->main, x, y, 0);
                    stage->set(stage->main, x, stage->height - 1, hazard != 0 ? hazard : 0);
                }
            };
            carve(stage->width / 3);
            if (stage->width >= 24) carve((2 * stage->width) / 3);
        }
        dropUnsupported();
        for (int x = 1; x < stage->width - 1; ++x) {
            if (standable(*stage, x, floorY) && placeActor(*stage, hero, x, floorY - 1)) break;
        }
        if (door) {
            for (int x = stage->width - 2; x >= 2; --x) {
                if (standable(*stage, x, floorY) && placeActor(*stage, door, x, floorY - 1)) break;
            }
        }
        if (enemy) {
            int placed = 0;
            for (int x = 4; x < stage->width - 4 && placed < 3; x += std::max(3, stage->width / 5)) {
                if (standable(*stage, x, floorY) && placeActor(*stage, enemy, x, floorY - 1)) placed++;
            }
        }
        if (coin) {
            for (int x = 2; x < stage->width - 2; x += 4)
                if (standable(*stage, x, floorY)) placeActor(*stage, coin, x, floorY - 1);
        }
        stage->complete = CompleteMode::TouchExit;
    }

    undoSuspend_ = false;
    genShape_ = savedShape;
    genX_ = savedX;
    genY_ = savedY;
    genLen_ = savedLen;
    genCoins_ = savedCoins;
    genExit_ = savedExit;
    genDress_ = savedDress;
    status_ = "Built a playable stage. Move or delete anything you don't want.";
}

void Editor::drawSheetPainter(std::vector<uint8_t>& rgba, int imgW, int ox, int oy, int w, int h) {
    const char* paintTools[] = {"Pencil", "Fill", "Pick", "Line"};
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        const bool selected = paintTool_ == i;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.48f, 0.14f, 1.f));
        if (ImGui::Button(paintTools[i])) paintTool_ = i;
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Zoom", &paintZoom_, 4, 32);
    for (int i = 0; i < 16; ++i) {
        if (i) ImGui::SameLine();
        ImGui::PushID(i);
        const ImVec4 color(kPalette[i].r / 255.f, kPalette[i].g / 255.f, kPalette[i].b / 255.f, 1.f);
        if (ImGui::ColorButton("swatch", color, colorIndex_ == i ? ImGuiColorEditFlags_None : 0, ImVec2(18, 18))) {
            colorIndex_ = i;
            paintR_ = kPalette[i].r;
            paintG_ = kPalette[i].g;
            paintB_ = kPalette[i].b;
        }
        ImGui::PopID();
    }
    ImGui::SameLine();
    float custom[3] = {paintR_ / 255.f, paintG_ / 255.f, paintB_ / 255.f};
    if (ImGui::ColorEdit3("##paint", custom, ImGuiColorEditFlags_NoInputs)) {
        colorIndex_ = 16;
        paintR_ = static_cast<uint8_t>(std::clamp(custom[0], 0.f, 1.f) * 255.f);
        paintG_ = static_cast<uint8_t>(std::clamp(custom[1], 0.f, 1.f) * 255.f);
        paintB_ = static_cast<uint8_t>(std::clamp(custom[2], 0.f, 1.f) * 255.f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Erase")) colorIndex_ = -1;
    ImGui::NewLine();
    const int scale = std::clamp(paintZoom_, 4, 32);
    auto indexOf = [&](int x, int y) -> size_t { return (static_cast<size_t>(oy + y) * imgW + ox + x) * 4; };
    ImGui::BeginChild("pixels", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = indexOf(x, y);
            const bool dark = ((x + y) & 1) == 0;
            ImU32 color = dark ? IM_COL32(32, 32, 40, 255) : IM_COL32(52, 52, 62, 255);
            if (i + 3 < rgba.size() && rgba[i + 3] > 0) color = IM_COL32(rgba[i], rgba[i + 1], rgba[i + 2], 255);
            const ImVec2 a(origin.x + x * scale, origin.y + y * scale);
            const ImVec2 b(a.x + scale, a.y + scale);
            draw->AddRectFilled(a, b, color);
        }
    }
    ImGui::Dummy(ImVec2(static_cast<float>(w * scale), static_cast<float>(h * scale)));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int px = static_cast<int>((mouse.x - origin.x) / scale);
    const int py = static_cast<int>((mouse.y - origin.y) / scale);
    const bool inside = px >= 0 && py >= 0 && px < w && py < h;
    const bool leftClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool leftDown = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool rightDown = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (leftClick && inside && paintTool_ == 3) {
        paintLine_ = true;
        paintX0_ = px;
        paintY0_ = py;
    }
    if (paintLine_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (inside) strokeSheet(rgba, imgW, ox, oy, w, h, paintX0_, paintY0_, px, py, false);
        paintLine_ = false;
    }
    if (leftClick && inside && paintTool_ == 1) floodSheet(rgba, imgW, ox, oy, w, h, px, py, false);
    if (rightDown && inside && paintTool_ == 1 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        floodSheet(rgba, imgW, ox, oy, w, h, px, py, true);
    if (leftClick && inside && paintTool_ == 2) pickSheet(rgba, imgW, ox, oy, w, h, px, py);
    if (inside && paintTool_ == 0 && (leftDown || rightDown)) plotSheet(rgba, imgW, ox, oy, w, h, px, py, rightDown);
    if (paintLine_ && inside) {
        const ImVec2 a(origin.x + (paintX0_ + 0.5f) * scale, origin.y + (paintY0_ + 0.5f) * scale);
        const ImVec2 b(origin.x + (px + 0.5f) * scale, origin.y + (py + 0.5f) * scale);
        draw->AddLine(a, b, IM_COL32(paintR_, paintG_, paintB_, 255), 2.f);
    }
    ImGui::EndChild();
}

void Editor::drawBottom() {
    Sprite* sprite = selectedSprite();
    Tileset* set = tileset();
    const bool tileOk = set && selectedTile_ > 0 && selectedTile_ <= static_cast<int>(set->tiles.size()) && set->tileW > 0 &&
                        set->tileH > 0 && set->imgW > 0;
    if (paintTarget_ == 1 && !tileOk) paintTarget_ = 0;
    if (!sprite && !tileOk) {
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::TextUnformatted("Select a sprite or a tile to open the pixel painter.");
        return;
    }
    if (sprite && tileOk) {
        if (paintTarget_ == 0) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.48f, 0.14f, 1.f));
        if (ImGui::Button("Sprite")) paintTarget_ = 0;
        if (paintTarget_ == 0) ImGui::PopStyleColor();
        ImGui::SameLine();
        if (paintTarget_ == 1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.48f, 0.14f, 1.f));
        if (ImGui::Button("Tile")) paintTarget_ = 1;
        if (paintTarget_ == 1) ImGui::PopStyleColor();
    } else if (!sprite) {
        paintTarget_ = 1;
    }
    if (paintTarget_ == 1 && tileOk) {
        const int columns = std::max(1, set->columns);
        const int index = selectedTile_ - 1;
        const int col = index % columns;
        const int row = index / columns;
        if (sprite) ImGui::SameLine();
        ImGui::Text("Tile %d", selectedTile_);
        ImGui::NewLine();
        drawSheetPainter(set->rgba, set->imgW, col * set->tileW, row * set->tileH, set->tileW, set->tileH);
        return;
    }
    if (!sprite) return;
    if (tileOk) ImGui::SameLine();
    ImGui::Text("Pixel painter — %s", sprite->name.c_str());
    ImGui::SetNextItemWidth(72.f);
    ImGui::InputInt("Frame w", &sprite->frameW);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.f);
    ImGui::InputInt("Frame h", &sprite->frameH);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.f);
    ImGui::InputInt("Frames", &sprite->frames);
    normalizeSprite(*sprite);
    spriteFrame_ = std::clamp(spriteFrame_, 0, sprite->frames - 1);
    ImGui::SetNextItemWidth(160.f);
    ImGui::SliderInt("Frame", &spriteFrame_, 0, sprite->frames - 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72);
    ImGui::InputFloat("FPS", &sprite->fps);
    ImGui::NewLine();
    if (spriteFrame_ > 0 && ImGui::Button("Copy previous frame")) copySpriteFrame(*sprite, spriteFrame_);
    drawSheetPainter(sprite->rgba, sprite->imgW, spriteFrame_ * sprite->frameW, 0, sprite->frameW, sprite->frameH);
}

void Editor::draw(SDL_Renderer* renderer) {
    drawRenderer_ = renderer;
    paintMap(renderer);
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("CabinetCore", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    drawMenu();
    drawToolbar();
    bool tileOk = false;
    if (const Tileset* set = tileset())
        tileOk = selectedTile_ > 0 && selectedTile_ <= static_cast<int>(set->tiles.size()) && set->tileW > 0 && set->tileH > 0;
    if (paintTarget_ == 1 && !tileOk) paintTarget_ = 0;
    const bool painter = (paintTarget_ == 1 && tileOk) || selectedSprite() != nullptr;
    ImGui::TextWrapped("%s", status_.c_str());
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float availX = std::max(1.f, avail.x);
    const float availY = std::max(1.f, avail.y);
    const float split = 6.f;
    float minLeft = 140.f;
    float minInspector = 160.f;
    float minCanvas = 96.f;
    float minBottom = 40.f;
    float minTop = 96.f;
    const float needX = minLeft + minInspector + minCanvas + split * 2.f;
    if (availX < needX) {
        const float scale = availX / needX;
        minLeft *= scale;
        minInspector *= scale;
        minCanvas *= scale;
    }
    const float needY = minTop + minBottom + split;
    if (availY < needY) {
        const float scale = availY / needY;
        minTop *= scale;
        minBottom *= scale;
    }
    const float maxLeft = std::max(minLeft, availX - minInspector - minCanvas - split * 2.f);
    const float shownLeft = clampLayout(layoutLeft_, minLeft, maxLeft);
    const float maxInspector = std::max(minInspector, availX - shownLeft - minCanvas - split * 2.f);
    const float shownInspector = clampLayout(layoutInspector_, minInspector, maxInspector);
    const float canvasW = std::max(1.f, availX - shownLeft - shownInspector - split * 2.f);
    const float autoPaint = std::min(300.f, std::max(220.f, io.DisplaySize.y * 0.32f));
    const float desiredBottom = painter ? (layoutBottomPaint_ < 0.f ? autoPaint : layoutBottomPaint_) : layoutBottom_;
    const float maxBottom = std::max(minBottom, availY - minTop - split);
    const float shownBottom = clampLayout(desiredBottom, minBottom, maxBottom);
    const float topH = std::max(1.f, availY - shownBottom - split);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
    ImGui::BeginChild("left", ImVec2(shownLeft, topH), true);
    uiLeftW_ = ImGui::GetWindowWidth();
    uiLeftH_ = ImGui::GetWindowHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 5.f));
    drawLeft();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::SameLine(0.f, 0.f);
    {
        float leftSize = shownLeft;
        bool resetEdge = false;
        layoutSplitter("split-left", true, split, topH, leftSize, minLeft, maxLeft, false, resetEdge);
        if (resetEdge) layoutLeft_ = kLayoutLeft;
        else if (leftSize != shownLeft) layoutLeft_ = leftSize;
    }
    ImGui::SameLine(0.f, 0.f);
    ImGui::BeginChild("canvas", ImVec2(canvasW, topH), true, ImGuiWindowFlags_HorizontalScrollbar);
    uiCanvasW_ = ImGui::GetWindowWidth();
    uiCanvasH_ = ImGui::GetWindowHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 5.f));
    drawCanvas();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::SameLine(0.f, 0.f);
    {
        float inspectorSize = shownInspector;
        bool resetEdge = false;
        const float maxInspectorDrag = std::max(minInspector, availX - shownLeft - minCanvas - split * 2.f);
        layoutSplitter("split-inspector", true, split, topH, inspectorSize, minInspector, maxInspectorDrag, true, resetEdge);
        if (resetEdge) layoutInspector_ = kLayoutInspector;
        else if (inspectorSize != shownInspector) layoutInspector_ = inspectorSize;
    }
    ImGui::SameLine(0.f, 0.f);
    ImGui::BeginChild("inspector", ImVec2(shownInspector, topH), true);
    uiInspectorW_ = ImGui::GetWindowWidth();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 5.f));
    drawInspector();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    {
        float bottomSize = shownBottom;
        bool resetEdge = false;
        layoutSplitter("split-bottom", false, split, availX, bottomSize, minBottom, maxBottom, true, resetEdge);
        if (resetEdge) {
            if (painter) layoutBottomPaint_ = -1.f;
            else layoutBottom_ = kLayoutBottom;
        } else if (bottomSize != shownBottom) {
            if (painter) layoutBottomPaint_ = bottomSize;
            else layoutBottom_ = bottomSize;
        }
    }
    ImGui::BeginChild("bottom", ImVec2(0.f, shownBottom), true);
    uiBottomH_ = ImGui::GetWindowHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 5.f));
    drawBottom();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    drawFilePick();
    ImGui::End();
    drawBlockEditor();
    drawSelfTest();

        if (!io.WantTextInput && !filePickOpen_) {
        if (ImGui::IsKeyPressed(ImGuiKey_F6)) buildGame();
        const bool ctrl = io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (!path_.empty()) saveProject(path_);
            else openFilePick(FilePick::SaveProject);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_1)) tool_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) tool_ = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) tool_ = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_4)) tool_ = 3;
        if (ImGui::IsKeyPressed(ImGuiKey_5)) tool_ = 4;
        if (ImGui::IsKeyPressed(ImGuiKey_6)) tool_ = 5;
    }
    trackUndo();
}

void Editor::togglePlay() {
    playing_ = !playing_;
    if (playing_) {
        const int index = project_.stages.empty() ? 0 : std::clamp(stageIndex_, 0, static_cast<int>(project_.stages.size()) - 1);
        engine_.start(project_, index, {}, true);
        audio_.open();
        const std::string stageName = project_.stages.empty() ? "this stage" : project_.stages[static_cast<size_t>(index)].name;
        status_ = "Playtest from " + stageName +
                  ". Start pauses. Button 1 restarts. Button 2 returns to the title. F3 shows hitboxes. F5 leaves the editor.";
    } else {
        audio_.stopLoops();
    }
}

void Editor::updatePlay(float dt, const Input& input) {
    engine_.update(dt, input);
    for (const std::string& id : engine_.takeSounds()) audio_.playId(project_, id, 0.8f, false);
    std::string music;
    if (engine_.takeMusic(music)) audio_.playId(project_, music, 0.45f, true);
}

void Editor::toggleHitboxes() { showHitboxes_ = !showHitboxes_; }

void Editor::renderPlay() {
    engine_.render();
    if (showHitboxes_) engine_.drawHitboxes();
}

const std::vector<uint8_t>& Editor::playPixels() const { return engine_.pixels(); }
int Editor::playWidth() const { return engine_.width(); }
int Editor::playHeight() const { return engine_.height(); }
bool Editor::playCrt() const { return project_.settings.crt; }
const std::string& Editor::windowTitle() const { return title_; }

void Editor::dropFile(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& ch : ext)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    if (ext == ".png") importPng(path);
    else if (ext == ".wav") importWav(path);
    else if (ext == ".ccplugin") loadEditorPlugin(path);
    else {
        std::string err;
        if (!openProject(path, err)) status_ = err;
    }
}

void Editor::fillPluginHost(PluginHost& host, int tx, int ty) {
    Stage* stage = currentStage();
    const Tileset* set = tileset();
    host = {};
    host.abi = kPluginAbi;
    host.user = this;
    host.tileX = tx;
    host.tileY = ty;
    host.stageW = stage ? stage->width : 0;
    host.stageH = stage ? stage->height : 0;
    host.tileW = set && set->tileW > 0 ? set->tileW : 16;
    host.tileH = set && set->tileH > 0 ? set->tileH : 16;
    host.log = [](void* user, const char* text) {
        if (text) static_cast<Editor*>(user)->status_ = text;
    };
    host.setStatus = host.log;
    host.paintTile = [](void* user, int x, int y, int tile) {
        auto* editor = static_cast<Editor*>(user);
        Stage* stage = editor->currentStage();
        if (!stage) return;
        editor->ensureLayers(*stage);
        const int value = std::max(0, std::min(tile, 65535));
        stage->set(editor->activeLayer(), x, y, static_cast<uint16_t>(value));
    };
    host.placeActor = [](void* user, const char* id, float x, float y) {
        auto* editor = static_cast<Editor*>(user);
        Stage* stage = editor->currentStage();
        if (!stage || !id || !id[0]) return;
        const ActorDef* actor = editor->project_.findActor(id);
        if (!actor) {
            editor->status_ = std::string("No actor named ") + id;
            return;
        }
        const Tileset* set = editor->tileset();
        const int tileW = set && set->tileW > 0 ? set->tileW : 16;
        const int tileH = set && set->tileH > 0 ? set->tileH : 16;
        const int tileX = static_cast<int>(std::floor(x / static_cast<float>(tileW)));
        const int tileY = static_cast<int>(std::floor(y / static_cast<float>(tileH)));
        editor->placeActor(*stage, actor, tileX, tileY);
    };
}

void Editor::loadEditorPlugin(const std::string& path) {
    std::string err;
    if (!loadPlugin(path, err)) {
        status_ = err;
        return;
    }
    rememberPlugin(path);
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    const std::string full = absolute.empty() ? path : absolute.string();
    const LoadedPlugin* plugin = nullptr;
    for (const auto& loaded : loadedPlugins())
        if (loaded.path == full) plugin = &loaded;
    if (!plugin) {
        status_ = "Loaded " + path;
        return;
    }
    if (plugin->kind == PluginKind::Tool)
        status_ = "Loaded " + plugin->title + ". Arm it from the Build tab, then click the scene.";
    else if (plugin->kind == PluginKind::Generator)
        status_ = "Loaded " + plugin->title + ". Run it from the Build tab.";
    else
        status_ = "Loaded " + plugin->title + ". It is in the Plugins list of the block editor.";
}

void Editor::runPluginTool(int tx, int ty) {
    const LoadedPlugin* plugin = findPlugin(armedPlugin_);
    if (!plugin || !plugin->desc || !plugin->desc->onTool) {
        status_ = "That plugin tool is not loaded.";
        armedPlugin_.clear();
        return;
    }
    pushUndo();
    PluginHost host;
    fillPluginHost(host, tx, ty);
    host.pluginName = plugin->name.c_str();
    const std::string before = status_;
    callPlugin(plugin->desc->onTool, host);
    if (status_ == before) status_ = "Used " + plugin->title + ".";
}

void Editor::runPluginGenerator(const std::string& name) {
    const LoadedPlugin* plugin = findPlugin(name);
    if (!plugin || !plugin->desc || !plugin->desc->onGenerate) {
        status_ = "That generator is not loaded.";
        return;
    }
    if (!currentStage()) {
        status_ = "Add a stage first.";
        return;
    }
    pushUndo();
    PluginHost host;
    fillPluginHost(host, 0, 0);
    host.pluginName = plugin->name.c_str();
    const std::string before = status_;
    callPlugin(plugin->desc->onGenerate, host);
    if (status_ == before) status_ = "Ran " + plugin->title + ".";
}

void Editor::saveProject(const std::string& path) {
    std::string err;
    std::string file = path;
    if (file.size() < 7 || file.substr(file.size() - 7) != ".ccproj") file += ".ccproj";
    if (!project_.save(file, err)) {
        status_ = err;
        return;
    }
    path_ = file;
    rememberProject(file);
    status_ = "Saved " + file;
}

void Editor::buildGame() {
    namespace fs = std::filesystem;
    const fs::path root = path_.empty() ? fs::path("dist") : fs::path(path_).parent_path() / "dist";
    std::string err;
    if (!exportGame(project_, root.string(), CABINET_PLAYER_PATH, err)) {
        status_ = err;
        return;
    }
    status_ = "Built " + (root / "game.dat").string() + " and " + (root / "cabinet.elf").string();
    if (!loadedPlugins().empty()) status_ += ". Plugins were copied beside the game.";
}

const char* Editor::filePickExtension() const {
    switch (filePick_) {
    case FilePick::LoadPlugin: return ".ccplugin";
    case FilePick::ImportPng: return ".png";
    case FilePick::ImportWav: return ".wav";
    default: return ".ccproj";
    }
}

std::vector<Editor::FileEntry> Editor::fileEntries(const std::string& directory, const std::string& extension) const {
    namespace fs = std::filesystem;
    std::vector<FileEntry> rows;
    std::error_code error;
    fs::directory_iterator it(directory, error);
    if (error) return rows;
    const fs::directory_iterator end;
    for (; it != end; it.increment(error)) {
        if (error) break;
        std::error_code status;
        const bool directoryEntry = it->is_directory(status);
        if (status) continue;
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (!directoryEntry) {
            auto lower = [](std::string text) {
                for (char& ch : text)
                    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                return text;
            };
            if (lower(it->path().extension().string()) != lower(extension)) continue;
        }
        rows.push_back(FileEntry{name, directoryEntry});
    }
    std::sort(rows.begin(), rows.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.directory != b.directory) return a.directory;
        std::string left = a.name;
        std::string right = b.name;
        for (char& ch : left)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        for (char& ch : right)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        return left < right;
    });
    return rows;
}

void Editor::openFilePick(FilePick kind) {
    namespace fs = std::filesystem;
    filePick_ = kind;
    filePickOpen_ = true;
    filePickRequest_ = true;
    fileReplace_ = false;
    fileNote_.clear();
    fileName_.clear();
    std::error_code error;
    if (fileDir_.empty() || !fs::is_directory(fileDir_, error)) {
        if (!path_.empty()) fileDir_ = fs::path(path_).parent_path().string();
        error.clear();
        if (fileDir_.empty() || !fs::is_directory(fileDir_, error)) fileDir_ = fs::current_path(error).string();
        if (fileDir_.empty()) fileDir_ = ".";
    }
    if (kind == FilePick::SaveProject) {
        if (!path_.empty()) fileName_ = fs::path(path_).filename().string();
        if (fileName_.empty()) fileName_ = "game.ccproj";
    }
}

std::string Editor::filePickPath() const {
    namespace fs = std::filesystem;
    fs::path file = fs::path(fileDir_) / fileName_;
    auto lower = [](std::string text) {
        for (char& ch : text)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        return text;
    };
    if (lower(file.extension().string()) != lower(filePickExtension())) file += filePickExtension();
    return file.lexically_normal().string();
}

void Editor::acceptFilePick(const std::string& path) {
    const FilePick kind = filePick_;
    filePickOpen_ = false;
    filePick_ = FilePick::None;
    fileReplace_ = false;
    fileNote_.clear();
    switch (kind) {
    case FilePick::OpenProject: {
        std::string err;
        if (!openProject(path, err)) status_ = err;
        break;
    }
    case FilePick::SaveProject:
        saveProject(path);
        break;
    case FilePick::LoadPlugin:
        loadEditorPlugin(path);
        break;
    case FilePick::ImportPng:
        importPng(path);
        break;
    case FilePick::ImportWav:
        importWav(path);
        break;
    default:
        break;
    }
}

void Editor::confirmFilePick() {
    if (fileName_.empty()) {
        fileNote_ = "Choose a file.";
        return;
    }
    namespace fs = std::filesystem;
    const std::string path = filePickPath();
    std::error_code error;
    if (filePick_ == FilePick::SaveProject) {
        if (fs::exists(path, error) && !fileReplace_) {
            fileNote_ = "That file is already there. Save again to replace it.";
            fileReplace_ = true;
            return;
        }
    } else if (!fs::is_regular_file(path, error)) {
        fileNote_ = "Choose a file.";
        return;
    }
    acceptFilePick(path);
}

void Editor::drawFilePick() {
    fileDialogDrawn_ = false;
    if (!filePickOpen_ || filePick_ == FilePick::None) return;
    const char* heading = "Open project";
    switch (filePick_) {
    case FilePick::SaveProject: heading = "Save project"; break;
    case FilePick::LoadPlugin: heading = "Load plugin"; break;
    case FilePick::ImportPng: heading = "Import picture"; break;
    case FilePick::ImportWav: heading = "Import sound"; break;
    default: break;
    }
    const std::string popup = std::string(heading) + "###filepick";
    if (filePickRequest_) {
        ImGui::OpenPopup(popup.c_str());
        filePickRequest_ = false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.f, 440.f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(popup.c_str(), &filePickOpen_, ImGuiWindowFlags_NoSavedSettings)) return;
    fileDialogDrawn_ = true;
    ImGui::TextWrapped("%s", fileDir_.c_str());
    namespace fs = std::filesystem;
    if (ImGui::Button("Up")) {
        const fs::path parent = fs::path(fileDir_).parent_path();
        if (!parent.empty() && parent != fs::path(fileDir_)) {
            fileDir_ = parent.string();
            fileReplace_ = false;
            fileNote_.clear();
        }
    }
    ImGui::SameLine();
    ImGui::Text("Showing %s files", filePickExtension());
    ImGui::BeginChild("filelist", ImVec2(0.f, 280.f), true);
    const std::vector<FileEntry> rows = fileEntries(fileDir_, filePickExtension());
    if (rows.empty()) ImGui::TextUnformatted("Nothing in this folder matches.");
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const FileEntry& row = rows[static_cast<size_t>(i)];
        ImGui::PushID(i);
        const std::string label = row.directory ? row.name + "/" : row.name;
        const bool selected = !row.directory && row.name == fileName_;
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (row.directory) {
                fileDir_ = (fs::path(fileDir_) / row.name).string();
                fileReplace_ = false;
                fileNote_.clear();
            } else {
                fileName_ = row.name;
                fileReplace_ = false;
                fileNote_.clear();
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) confirmFilePick();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (editString("File", fileName_)) {
        fileReplace_ = false;
        fileNote_.clear();
    }
    if (!fileNote_.empty()) ImGui::TextWrapped("%s", fileNote_.c_str());
    const char* action = filePick_ == FilePick::SaveProject ? "Save" : "Open";
    if (ImGui::Button(action)) confirmFilePick();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        filePickOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    if (!filePickOpen_) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void Editor::importPng(const std::string& path) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        status_ = "Could not read " + path;
        return;
    }
    Sprite sprite;
    sprite.id = uniqueId(project_, "import");
    sprite.name = std::filesystem::path(path).stem().string();
    sprite.frameW = std::min(width, 128);
    sprite.frameH = height;
    sprite.frames = std::max(1, width / sprite.frameW);
    sprite.imgW = width;
    sprite.imgH = height;
    sprite.rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    project_.sprites.push_back(std::move(sprite));
    selectedSprite_ = static_cast<int>(project_.sprites.size()) - 1;
    status_ = "Imported " + path;
}

void Editor::importWav(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        status_ = "Could not open " + path;
        return;
    }
    char riff[4];
    in.read(riff, 4);
    in.ignore(4);
    char wave[4];
    in.read(wave, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0 || std::strncmp(wave, "WAVE", 4) != 0) {
        status_ = "Not a WAV file";
        return;
    }
    int rate = 44100, channels = 1, bits = 16;
    std::vector<int16_t> samples;
    while (in) {
        char id[4];
        uint32_t size = 0;
        in.read(id, 4);
        in.read(reinterpret_cast<char*>(&size), 4);
        if (!in) break;
        if (std::strncmp(id, "fmt ", 4) == 0) {
            uint16_t format = 0, ch = 0, bps = 0;
            uint32_t sampleRate = 0;
            in.read(reinterpret_cast<char*>(&format), 2);
            in.read(reinterpret_cast<char*>(&ch), 2);
            in.read(reinterpret_cast<char*>(&sampleRate), 4);
            in.ignore(6);
            in.read(reinterpret_cast<char*>(&bps), 2);
            if (size > 16) in.ignore(size - 16);
            rate = static_cast<int>(sampleRate);
            channels = ch;
            bits = bps;
            if (format != 1) {
                status_ = "Only uncompressed PCM WAV files can be imported";
                return;
            }
        } else if (std::strncmp(id, "data", 4) == 0) {
            std::vector<char> raw(size);
            in.read(raw.data(), size);
            if (bits == 16) {
                const size_t count = size / 2;
                samples.resize(count / std::max(1, channels));
                for (size_t i = 0; i < samples.size(); ++i) {
                    int mixed = 0;
                    for (int c = 0; c < channels; ++c) {
                        int16_t sample = 0;
                        std::memcpy(&sample, raw.data() + (i * channels + c) * 2, 2);
                        mixed += sample;
                    }
                    samples[i] = static_cast<int16_t>(mixed / std::max(1, channels));
                }
            } else if (bits == 8) {
                samples.resize(size / std::max(1, channels));
                for (size_t i = 0; i < samples.size(); ++i) {
                    int mixed = 0;
                    for (int c = 0; c < channels; ++c)
                        mixed += static_cast<unsigned char>(raw[i * channels + c]) - 128;
                    samples[i] = static_cast<int16_t>(mixed / std::max(1, channels) * 256);
                }
            } else {
                status_ = "WAV must be 8-bit or 16-bit PCM";
                return;
            }
        } else {
            in.ignore(size + (size & 1));
        }
    }
    Sound sound;
    sound.id = uniqueId(project_, "sound");
    sound.name = std::filesystem::path(path).stem().string();
    sound.sampleRate = rate;
    sound.samples = std::move(samples);
    project_.sounds.push_back(std::move(sound));
    selectedSound_ = static_cast<int>(project_.sounds.size()) - 1;
    status_ = "Imported " + path;
}

void Editor::addActor(ActorKind kind) {
    ActorDef actor;
    actor.kind = kind;
    actor.id = uniqueId(project_, kind == ActorKind::Gun ? "gun" : kind == ActorKind::Item ? "item" : "actor");
    actor.name = kindName(kind);
    actor.spriteId = project_.sprites.empty() ? "" : project_.sprites[0].id;
    actor.behavior.gravity = kind == ActorKind::Enemy;
    actor.behavior.touchDamage = kind == ActorKind::Enemy || kind == ActorKind::Boss || kind == ActorKind::Gun ? 1 : 0;
    if (kind == ActorKind::Item) actor.pickup = Pickup::Coin;
    if (kind == ActorKind::Gun) {
        actor.behavior.gravity = false;
        actor.behavior.lifetime = 1.2f;
        actor.behavior.shotSpeed = 200;
        actor.hitW = 4;
        actor.hitH = 4;
    }
    project_.actors.push_back(actor);
    selectedActor_ = static_cast<int>(project_.actors.size()) - 1;
}

void Editor::addStage(bool duplicate) {
    if (duplicate && currentStage()) {
        Stage copy = *currentStage();
        copy.id = uniqueId(project_, copy.id.empty() ? "stage" : copy.id);
        copy.name += " copy";
        project_.stages.push_back(copy);
    } else {
        Stage stage;
        stage.id = uniqueId(project_, "stage");
        stage.name = "Stage " + std::to_string(project_.stages.size() + 1);
        stage.tilesetId = project_.tilesets.empty() ? "main" : project_.tilesets[0].id;
        stage.resize(24, 15);
        for (int x = 0; x < stage.width; ++x) stage.set(stage.main, x, 13, 1);
        if (!project_.actors.empty()) {
            Placement placement;
            placement.uid = project_.freshUid();
            placement.actorId = project_.actors[0].id;
            for (const auto& actor : project_.actors)
                if (actor.kind == ActorKind::Hero) placement.actorId = actor.id;
            placement.x = 32;
            placement.y = 12 * 16;
            stage.placements.push_back(placement);
        }
        project_.stages.push_back(stage);
    }
    stageIndex_ = static_cast<int>(project_.stages.size()) - 1;
}

void Editor::deleteCurrentStage() {
    if (project_.stages.size() <= 1) {
        status_ = "Keep at least one stage.";
        return;
    }
    project_.stages.erase(project_.stages.begin() + stageIndex_);
    stageIndex_ = std::min(stageIndex_, static_cast<int>(project_.stages.size()) - 1);
    selectedPlacement_ = -1;
}

void Editor::resizeCurrentStage(int width, int height) {
    Stage* stage = currentStage();
    if (!stage) return;
    pushUndo();
    stage->resize(std::clamp(width, 5, 200), std::clamp(height, 5, 200));
}

}  // namespace cc
