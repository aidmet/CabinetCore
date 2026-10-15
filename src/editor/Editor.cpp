#include "editor/Editor.hpp"

#include "cabinet_player_path.h"
#include "common/Export.hpp"
#include "demo/Demo.hpp"

#include <SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
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

}  // namespace

Editor::Editor() { loadDemo(); }

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
            if (ImGui::MenuItem("Platform")) project_ = makeTemplate(PhysicsMode::Platform, "Platform");
            if (ImGui::MenuItem("Run and gun")) project_ = makeTemplate(PhysicsMode::RunAndGun, "Run and Gun");
            if (ImGui::MenuItem("Vertical shooter")) project_ = makeTemplate(PhysicsMode::ShooterVert, "Vertical Shooter");
            if (ImGui::MenuItem("Horizontal shooter")) project_ = makeTemplate(PhysicsMode::ShooterHorz, "Horizontal Shooter");
            if (ImGui::MenuItem("Maze")) project_ = makeTemplate(PhysicsMode::Maze, "Maze");
            if (ImGui::MenuItem("Top-down")) project_ = makeTemplate(PhysicsMode::TopDown, "Top Down");
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
        const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool release = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        if (click) {
            pushUndo();
            stroking_ = true;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            selectedTile_ = stage->get(activeLayer(), tx, ty);
            status_ = "Picked tile " + std::to_string(selectedTile_);
        }
        if (click || (down && (tool_ == 1 || tool_ == 2))) applyTool(tx, ty, click, true);
        if (release && tool_ == 4) applyTool(tx, ty, false, false);
        if (tool_ == 0 && selectedPlacement_ >= 0 && down &&
            selectedPlacement_ < static_cast<int>(stage->placements.size())) {
            stage->placements[selectedPlacement_].x = localX - tileW * 0.5f;
            stage->placements[selectedPlacement_].y = localY - tileH * 0.5f;
        }
        if (release) stroking_ = false;
    }
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
        ImGui::TextUnformatted("Select a sprite to pixel-edit it. Right-click the map to pick a tile.");
        return;
    }
    ImGui::Text("Sprite %s", sprite->name.c_str());
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
    const int scale = 12;
    const int originX = spriteFrame_ * sprite->frameW;
    ImGui::BeginChild("pixels", ImVec2(sprite->frameW * scale + 8.f, sprite->frameH * scale + 8.f), true);
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
    ImGui::Dummy(ImVec2(sprite->frameW * scale, sprite->frameH * scale));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int x = static_cast<int>((mouse.x - origin.x) / scale);
        const int y = static_cast<int>((mouse.y - origin.y) / scale);
        if (x >= 0 && y >= 0 && x < sprite->frameW && y < sprite->frameH) {
            const size_t i = (static_cast<size_t>(y) * sprite->imgW + originX + x) * 4;
            if (colorIndex_ < 0) {
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
    ImGui::TextWrapped("%s", status_.c_str());
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
    const float bottom = 210.f;
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
        engine_.start(project_);
        audio_.open();
        status_ = "Playtest. Start pauses. Button 1 restarts. Button 2 returns to the title. F5 leaves the editor.";
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
