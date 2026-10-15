#include "editor/Editor.hpp"

#include "cabinet_player_path.h"
#include "common/Export.hpp"
#include "demo/Demo.hpp"

#include <SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
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
const char* kPickups[] = {"None", "Coin", "Heal", "Extra life", "Weapon", "Invincibility", "Key", "Power pellet"};
const char* kPatterns[] = {"None", "Idle", "Patrol", "Chase", "Shooter", "Sine", "Bounce", "Hopper", "Dive", "Maze ghost", "Boss", "Turret", "Fly"};
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

bool editString(const char* label, std::string& value) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (ImGui::InputText(label, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

bool editEnum(const char* label, uint32_t& value, const char* const* names, int count) {
    int current = static_cast<int>(value);
    if (current < 0 || current >= count) current = 0;
    bool changed = false;
    if (ImGui::BeginCombo(label, names[current])) {
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

std::string lastProjectFile() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return std::string(home) + "/.config/cabinetcore/last-project";
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

void Editor::pushUndo() {
    Stage* stage = currentStage();
    if (!stage) return;
    ensureLayers(*stage);
    Snapshot snap;
    snap.width = stage->width;
    snap.height = stage->height;
    snap.main = stage->main;
    snap.back = stage->back;
    snap.front = stage->front;
    snap.placements = stage->placements;
    undo_.push_back(std::move(snap));
    if (undo_.size() > 40) undo_.erase(undo_.begin());
    redo_.clear();
}

void Editor::undo() {
    Stage* stage = currentStage();
    if (!stage || undo_.empty()) return;
    Snapshot current;
    current.width = stage->width;
    current.height = stage->height;
    current.main = stage->main;
    current.back = stage->back;
    current.front = stage->front;
    current.placements = stage->placements;
    redo_.push_back(std::move(current));
    const Snapshot snap = undo_.back();
    undo_.pop_back();
    stage->resize(snap.width, snap.height);
    stage->main = snap.main;
    stage->back = snap.back;
    stage->front = snap.front;
    stage->placements = snap.placements;
    status_ = "Undid stage edit";
}

void Editor::redo() {
    Stage* stage = currentStage();
    if (!stage || redo_.empty()) return;
    Snapshot current;
    current.width = stage->width;
    current.height = stage->height;
    current.main = stage->main;
    current.back = stage->back;
    current.front = stage->front;
    current.placements = stage->placements;
    undo_.push_back(std::move(current));
    const Snapshot snap = redo_.back();
    redo_.pop_back();
    stage->resize(snap.width, snap.height);
    stage->main = snap.main;
    stage->back = snap.back;
    stage->front = snap.front;
    stage->placements = snap.placements;
    status_ = "Redid stage edit";
}

void Editor::flood(int tx, int ty, uint16_t from, uint16_t to) {
    Stage* stage = currentStage();
    if (!stage || from == to) return;
    std::vector<std::pair<int, int>> stack;
    stack.push_back({tx, ty});
    int guard = stage->width * stage->height + 4;
    while (!stack.empty() && guard-- > 0) {
        const int x = stack.back().first;
        const int y = stack.back().second;
        stack.pop_back();
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
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            const std::string file = pickFile(false, "*.ccproj");
            std::string err;
            if (!file.empty() && !openProject(file, err)) status_ = err;
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            if (path_.empty()) {
                const std::string file = pickFile(true, "*.ccproj");
                if (!file.empty()) saveProject(file);
            } else saveProject(path_);
        }
        if (ImGui::MenuItem("Save As...")) {
            const std::string file = pickFile(true, "*.ccproj");
            if (!file.empty()) saveProject(file);
        }
        if (ImGui::MenuItem("Build game", "F6")) buildGame();
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
        if (ImGui::MenuItem("Zoom in")) zoom_ = std::min(6, zoom_ + 1);
        if (ImGui::MenuItem("Zoom out")) zoom_ = std::max(1, zoom_ - 1);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
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
    ImGui::SameLine();
    ImGui::Text("Zoom %d", zoom_);
    ImGui::SameLine();
    ImGui::RadioButton("Backdrop", &layer_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Collision", &layer_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Foreground", &layer_, 2);
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
    return (flags & (TileSolid | TileOneWay)) != 0;
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
    int keyX = x0;
    int doorX = x1;
    int standY = std::max(0, y1 - 1);
    if (!spots.empty()) {
        keyX = spots.front().first;
        standY = spots.front().second;
        doorX = spots.back().first;
        int doorY = spots.back().second;
        if (key && door && keyX == doorX && standY == doorY) {
            if (doorX + 1 < stage->width) doorX++;
            else if (keyX > 0) keyX--;
        }
        if (key) placeActor(*stage, key, keyX, standY);
        if (door) placeActor(*stage, door, doorX, doorY);
    } else {
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
        if (key) placeActor(*stage, key, keyX, standY);
        if (door) placeActor(*stage, door, doorX, standY);
    }
    status_ = "Placed the key and the door.";
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
            if ((flags & TileSolid) && !(flags & (TileOneWay | TileLadder | TileHazard | TileWater | TileDestructible | TileIce)) &&
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
    if (ImGui::BeginTabBar("left", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Stages")) {
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
            if (ImGui::Button("Delete") && project_.stages.size() > 1) {
                project_.stages.erase(project_.stages.begin() + stageIndex_);
                stageIndex_ = std::min(stageIndex_, static_cast<int>(project_.stages.size()) - 1);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Tiles")) {
            const Tileset* set = tileset();
            const int tileW = set ? set->tileW : 16;
            const int tileH = set ? set->tileH : 16;
            const int count = set ? static_cast<int>(set->tiles.size()) : 0;
            if (ImGui::Selectable("Empty", selectedTile_ == 0)) selectedTile_ = 0;
            for (int i = 1; i <= count; ++i) {
                ImGui::PushID(i);
                const bool on = selectedTile_ == i;
                if (ImGui::Selectable(("Tile " + std::to_string(i)).c_str(), on)) selectedTile_ = i;
                ImGui::PopID();
                (void)tileW;
                (void)tileH;
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Actors")) {
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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sprites")) {
            for (int i = 0; i < static_cast<int>(project_.sprites.size()); ++i) {
                if (ImGui::Selectable((project_.sprites[i].name + "##sp" + std::to_string(i)).c_str(), selectedSprite_ == i))
                    selectedSprite_ = i;
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
            if (ImGui::Button("Import PNG")) {
                const std::string file = pickFile(false, "*.png");
                if (!file.empty()) importPng(file);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sounds")) {
            for (int i = 0; i < static_cast<int>(project_.sounds.size()); ++i) {
                const std::string label = project_.sounds[i].name + (project_.sounds[i].music ? "  (music)" : "");
                if (ImGui::Selectable((label + "##so" + std::to_string(i)).c_str(), selectedSound_ == i)) selectedSound_ = i;
            }
            if (selectedSound_ >= 0 && ImGui::Button("Preview")) {
                audio_.open();
                audio_.play(&project_.sounds[selectedSound_], 0.8f, project_.sounds[selectedSound_].music);
            }
            ImGui::SameLine();
            if (ImGui::Button("Import WAV")) {
                const std::string file = pickFile(false, "*.wav");
                if (!file.empty()) importWav(file);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Build")) {
            drawBuildTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Game")) {
            editString("Title", project_.settings.title);
            editString("Author", project_.settings.author);
            ImGui::InputInt("View width", &project_.settings.viewW);
            ImGui::InputInt("View height", &project_.settings.viewH);
            ImGui::InputInt("Window scale", &project_.settings.scale);
            ImGui::InputInt("Lives", &project_.settings.lives);
            ImGui::InputInt("Health", &project_.settings.maxHealth);
            project_.settings.viewW = std::clamp(project_.settings.viewW, 160, 640);
            project_.settings.viewH = std::clamp(project_.settings.viewH, 120, 480);
            project_.settings.scale = std::clamp(project_.settings.scale, 1, 6);
            ImGui::Checkbox("CRT scanlines", &project_.settings.crt);
            ImGui::Checkbox("Screen shake", &project_.settings.screenShake);
            editString("Start stage id", project_.settings.startStage);
            title_ = "CabinetCore — " + project_.settings.title;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void Editor::drawCanvas() {
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
        status_ = "Tile " + std::to_string(tx) + "," + std::to_string(ty) + "   " + kTools[tool_] +
                  "   layer " + std::string(layer_ == 0 ? "backdrop" : layer_ == 2 ? "foreground" : "collision");
        if (genDrag_ == 1) status_ = "Drag the stretch, then release.";
        if (genDrag_ == 2) status_ = "Drag the area, then release.";
        if (genDrag_ == 3) status_ = "Drag the chunk to copy, then release.";
        if (genDrag_ == 4) status_ = "Click where the chunk should land.";
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
        }
        if (tool_ == 0 && selectedPlacement_ >= 0 && down && !click &&
            selectedPlacement_ < static_cast<int>(stage->placements.size())) {
            if (!draggingActor_) {
                pushUndo();
                draggingActor_ = true;
            }
            stage->placements[selectedPlacement_].x = localX - tileW * 0.5f;
            stage->placements[selectedPlacement_].y = localY - tileH * 0.5f;
        }
        if (release) {
            stroking_ = false;
            draggingActor_ = false;
        }
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
    uint32_t physics = static_cast<uint32_t>(stage.physics);
    uint32_t scroll = static_cast<uint32_t>(stage.scroll);
    uint32_t complete = static_cast<uint32_t>(stage.complete);
    if (editEnum("Physics", physics, kPhysics, 6)) stage.physics = static_cast<PhysicsMode>(physics);
    if (editEnum("Scroll", scroll, kScroll, 6)) stage.scroll = static_cast<ScrollMode>(scroll);
    if (editEnum("Complete", complete, kComplete, 4)) stage.complete = static_cast<CompleteMode>(complete);
    ImGui::Checkbox("Intermission", &stage.intermission);
    ImGui::InputFloat("Auto scroll", &stage.autoScroll);
    ImGui::InputFloat("Gravity", &stage.gravity);
    ImGui::InputFloat("Jump", &stage.jumpVel);
    ImGui::InputFloat("Player speed", &stage.playerSpeed);
    static Stage* synced = nullptr;
    static int newW = 20;
    static int newH = 15;
    if (synced != &stage) {
        synced = &stage;
        newW = stage.width;
        newH = stage.height;
    }
    ImGui::InputInt("Width", &newW);
    ImGui::InputInt("Height", &newH);
    if (ImGui::Button("Resize stage")) {
        pushUndo();
        stage.resize(std::clamp(newW, 5, 200), std::clamp(newH, 5, 200));
    }
}

void Editor::drawActorEditor(ActorDef& actor) {
    editString("Name", actor.name);
    ImGui::Text("Id: %s", actor.id.c_str());
    uint32_t kind = static_cast<uint32_t>(actor.kind);
    uint32_t pickup = static_cast<uint32_t>(actor.pickup);
    uint32_t pattern = static_cast<uint32_t>(actor.behavior.pattern);
    uint32_t aim = static_cast<uint32_t>(actor.behavior.aim);
    if (editEnum("Kind", kind, kKinds, 7)) actor.kind = static_cast<ActorKind>(kind);
    if (editEnum("Behavior", pattern, kPatterns, 13)) actor.behavior.pattern = static_cast<AiPattern>(pattern);
    if (editEnum("Aim", aim, kAims, 5)) actor.behavior.aim = static_cast<AimMode>(aim);
    if (editEnum("Pickup", pickup, kPickups, 8)) actor.pickup = static_cast<Pickup>(pickup);
    if (ImGui::BeginCombo("Sprite", actor.spriteId.c_str())) {
        for (const auto& sprite : project_.sprites) {
            if (ImGui::Selectable(sprite.id.c_str(), sprite.id == actor.spriteId)) actor.spriteId = sprite.id;
        }
        ImGui::EndCombo();
    }
    editString("Projectile id", actor.projectileId);
    editString("Drop id", actor.dropActorId);
    ImGui::InputFloat("Hit width", &actor.hitW);
    ImGui::InputFloat("Hit height", &actor.hitH);
    ImGui::InputFloat("Speed", &actor.behavior.speed);
    ImGui::InputFloat("Fire every", &actor.behavior.fireInterval);
    ImGui::InputFloat("Shot speed", &actor.behavior.shotSpeed);
    ImGui::InputFloat("Spread", &actor.behavior.spread);
    ImGui::InputInt("Burst", &actor.behavior.burst);
    ImGui::InputInt("HP", &actor.behavior.hp);
    ImGui::InputInt("Touch damage", &actor.behavior.touchDamage);
    ImGui::InputInt("Score", &actor.behavior.score);
    ImGui::InputInt("Pickup amount", &actor.pickupAmount);
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
    flag("Solid", TileSolid);
    flag("One-way", TileOneWay);
    flag("Ladder", TileLadder);
    flag("Hazard", TileHazard);
    flag("Water", TileWater);
    flag("Ice", TileIce);
    flag("Destructible", TileDestructible);
    ImGui::InputInt("Anim frames", &tile.frames);
    ImGui::InputFloat("Anim fps", &tile.fps);
}

void Editor::drawInspector() {
    Stage* stage = currentStage();
    if (!stage) return;
    if (ImGui::CollapsingHeader("Stage", ImGuiTreeNodeFlags_DefaultOpen)) drawStageSettings(*stage);
    if (ImGui::CollapsingHeader("Tile", ImGuiTreeNodeFlags_DefaultOpen)) drawTileFlags();
    if (selectedPlacement_ >= 0 && selectedPlacement_ < static_cast<int>(stage->placements.size())) {
        if (ImGui::CollapsingHeader("Placed actor", ImGuiTreeNodeFlags_DefaultOpen)) {
            Placement& placement = stage->placements[selectedPlacement_];
            ImGui::InputFloat("X", &placement.x);
            ImGui::InputFloat("Y", &placement.y);
            if (ImGui::BeginCombo("Actor", placement.actorId.c_str())) {
                for (const auto& actor : project_.actors) {
                    if (ImGui::Selectable(actor.name.c_str(), actor.id == placement.actorId)) placement.actorId = actor.id;
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Remove from stage")) {
                pushUndo();
                stage->placements.erase(stage->placements.begin() + selectedPlacement_);
                selectedPlacement_ = -1;
            }
        }
    }
    if (selectedActor_ >= 0 && selectedActor_ < static_cast<int>(project_.actors.size())) {
        if (ImGui::CollapsingHeader("Behavior", ImGuiTreeNodeFlags_DefaultOpen))
            drawActorEditor(project_.actors[selectedActor_]);
    }
}

void Editor::drawBottom() {
    Sprite* sprite = selectedSprite();
    if (!sprite) {
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::TextUnformatted("Select a sprite to open the pixel painter. Right-click the map to pick a tile.");
        return;
    }
    ImGui::Text("Pixel painter — %s", sprite->name.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("Frame w", &sprite->frameW);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("Frame h", &sprite->frameH);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("Frames", &sprite->frames);
    sprite->frameW = std::clamp(sprite->frameW, 1, 128);
    sprite->frameH = std::clamp(sprite->frameH, 1, 128);
    sprite->frames = std::clamp(sprite->frames, 1, 32);
    const int imgW = sprite->frameW * sprite->frames;
    const int imgH = sprite->frameH;
    if (sprite->imgW != imgW || sprite->imgH != imgH || static_cast<int>(sprite->rgba.size()) != imgW * imgH * 4) {
        std::vector<uint8_t> next(static_cast<size_t>(imgW) * imgH * 4, 0);
        const int copyW = std::min(imgW, sprite->imgW);
        const int copyH = std::min(imgH, sprite->imgH);
        for (int y = 0; y < copyH; ++y) {
            if (sprite->imgW <= 0) break;
            std::memcpy(next.data() + static_cast<size_t>(y) * imgW * 4,
                        sprite->rgba.data() + static_cast<size_t>(y) * sprite->imgW * 4, copyW * 4);
        }
        sprite->rgba.swap(next);
        sprite->imgW = imgW;
        sprite->imgH = imgH;
    }
    spriteFrame_ = std::clamp(spriteFrame_, 0, sprite->frames - 1);
    ImGui::SliderInt("Frame", &spriteFrame_, 0, sprite->frames - 1);
    ImGui::InputFloat("FPS", &sprite->fps);
    for (int i = 0; i < 16; ++i) {
        if (i) ImGui::SameLine();
        ImGui::PushID(i);
        const ImVec4 color(kPalette[i].r / 255.f, kPalette[i].g / 255.f, kPalette[i].b / 255.f, 1.f);
        if (ImGui::ColorButton("swatch", color, 0, ImVec2(18, 18))) {
            colorIndex_ = i;
            paintR_ = kPalette[i].r;
            paintG_ = kPalette[i].g;
            paintB_ = kPalette[i].b;
        }
        ImGui::PopID();
    }
    ImGui::SameLine();
    if (ImGui::Button("Erase")) colorIndex_ = -1;
    ImGui::SameLine();
    ImGui::TextUnformatted("Left click draws. Right click erases.");
    const ImVec2 room = ImGui::GetContentRegionAvail();
    int scale = 8;
    if (sprite->frameW > 0 && sprite->frameH > 0) {
        const int fitW = static_cast<int>((room.x - 16.f) / sprite->frameW);
        const int fitH = static_cast<int>((room.y - 8.f) / sprite->frameH);
        scale = std::clamp(std::min(fitW, fitH), 4, 16);
    }
    const int originX = spriteFrame_ * sprite->frameW;
    ImGui::BeginChild("pixels", ImVec2(0, 0), true);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (int y = 0; y < sprite->frameH; ++y) {
        for (int x = 0; x < sprite->frameW; ++x) {
            const size_t i = (static_cast<size_t>(y) * sprite->imgW + originX + x) * 4;
            ImU32 color = IM_COL32(20, 20, 28, 255);
            if (i + 3 < sprite->rgba.size() && sprite->rgba[i + 3] > 0)
                color = IM_COL32(sprite->rgba[i], sprite->rgba[i + 1], sprite->rgba[i + 2], 255);
            const ImVec2 a(origin.x + x * scale, origin.y + y * scale);
            const ImVec2 b(a.x + scale - 1, a.y + scale - 1);
            draw->AddRectFilled(a, b, color);
        }
    }
    ImGui::Dummy(ImVec2(static_cast<float>(sprite->frameW * scale), static_cast<float>(sprite->frameH * scale)));
    const bool drawDown = ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool eraseDown = ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (drawDown || eraseDown) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int x = static_cast<int>((mouse.x - origin.x) / scale);
        const int y = static_cast<int>((mouse.y - origin.y) / scale);
        if (x >= 0 && y >= 0 && x < sprite->frameW && y < sprite->frameH) {
            const size_t i = (static_cast<size_t>(y) * sprite->imgW + originX + x) * 4;
            if (eraseDown || colorIndex_ < 0) {
                sprite->rgba[i] = sprite->rgba[i + 1] = sprite->rgba[i + 2] = sprite->rgba[i + 3] = 0;
            } else {
                sprite->rgba[i] = paintR_;
                sprite->rgba[i + 1] = paintG_;
                sprite->rgba[i + 2] = paintB_;
                sprite->rgba[i + 3] = 255;
            }
        }
    }
    ImGui::EndChild();
}

void Editor::draw(SDL_Renderer* renderer) {
    paintMap(renderer);
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("CabinetCore", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    drawMenu();
    drawToolbar();
    const float bottom = selectedSprite() ? std::min(360.f, std::max(180.f, io.DisplaySize.y * 0.38f)) : 64.f;
    ImGui::TextWrapped("%s", status_.c_str());
    ImGui::BeginChild("left", ImVec2(320, -bottom), true);
    drawLeft();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("canvas", ImVec2(-340, -bottom), true, ImGuiWindowFlags_HorizontalScrollbar);
    drawCanvas();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("inspector", ImVec2(0, -bottom), true);
    drawInspector();
    ImGui::EndChild();
    ImGui::BeginChild("bottom", ImVec2(0, 0), true);
    drawBottom();
    ImGui::EndChild();
    ImGui::End();
    drawBlockEditor();

        if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F6)) buildGame();
        const bool ctrl = io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (!path_.empty()) saveProject(path_);
            else {
                const std::string file = pickFile(true, "*.ccproj");
                if (!file.empty()) saveProject(file);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_1)) tool_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) tool_ = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) tool_ = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_4)) tool_ = 3;
        if (ImGui::IsKeyPressed(ImGuiKey_5)) tool_ = 4;
        if (ImGui::IsKeyPressed(ImGuiKey_6)) tool_ = 5;
    }
}

void Editor::togglePlay() {
    playing_ = !playing_;
    if (playing_) {
        const int index = project_.stages.empty() ? 0 : std::clamp(stageIndex_, 0, static_cast<int>(project_.stages.size()) - 1);
        engine_.start(project_, index, {}, true);
        audio_.open();
        const std::string stageName = project_.stages.empty() ? "this stage" : project_.stages[static_cast<size_t>(index)].name;
        status_ = "Playtest from " + stageName + ". Start pauses. Button 1 restarts. Button 2 returns to the title. F5 leaves the editor.";
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

void Editor::renderPlay() { engine_.render(); }

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
    else {
        std::string err;
        if (!openProject(path, err)) status_ = err;
    }
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
}

std::string Editor::pickFile(bool save, const char* filter) {
    const std::string command = std::string("zenity --file-selection ") + (save ? "--save --confirm-overwrite " : "") +
                                "--file-filter='" + filter + "' 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        status_ = "Install zenity for a file dialog, or drop a file onto the window.";
        return {};
    }
    char buffer[1024];
    std::string path;
    while (std::fgets(buffer, sizeof(buffer), pipe)) path += buffer;
    pclose(pipe);
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
    return path;
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

}  // namespace cc
