#pragma once

#include "common/AudioDevice.hpp"
#include "common/Canvas.hpp"
#include "common/Types.hpp"
#include "player/Engine.hpp"

#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace cc {

class Editor {
public:
    Editor();
    void loadDemo();
    bool openProject(const std::string& path, std::string& err);
    void dropFile(const std::string& path);
    void draw(SDL_Renderer* renderer);
    bool playing() const { return playing_; }
    void togglePlay();
    void updatePlay(float dt, const Input& input);
    void renderPlay();
    const std::vector<uint8_t>& playPixels() const;
    int playWidth() const;
    int playHeight() const;
    bool playCrt() const;
    const std::string& windowTitle() const;

private:
    struct Snapshot {
        int width = 0;
        int height = 0;
        std::vector<uint16_t> main;
        std::vector<uint16_t> back;
        std::vector<uint16_t> front;
        std::vector<Placement> placements;
    };

    Stage* currentStage();
    const Stage* currentStage() const;
    std::vector<uint16_t>& activeLayer();
    void ensureLayers(Stage& stage);
    void pushUndo();
    void undo();
    void redo();
    void paintMap(SDL_Renderer* renderer);
    void drawMenu();
    void drawToolbar();
    void drawLeft();
    void drawCanvas();
    void drawInspector();
    void drawBottom();
    void drawStageSettings(Stage& stage);
    void drawActorEditor(ActorDef& actor);
    void drawBlockEditor();
    void drawTileFlags();
    void applyTool(int tx, int ty, bool click, bool down);
    int placementAt(float x, float y) const;
    void flood(int tx, int ty, uint16_t from, uint16_t to);
    void saveProject(const std::string& path);
    void buildGame();
    void importPng(const std::string& path);
    void importWav(const std::string& path);
    std::string pickFile(bool save, const char* filter);
    void addActor(ActorKind kind);
    void addStage(bool duplicate);
    Tileset* tileset();
    Sprite* selectedSprite();

    Project project_;
    Engine engine_;
    AudioDevice audio_;
    Canvas map_;
    std::string path_;
    std::string status_ = "Demo loaded. F5 plays, F6 builds game.dat and cabinet.elf.";
    std::string title_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    SDL_Texture* mapTex_ = nullptr;
    int mapTexW_ = 0;
    int mapTexH_ = 0;
    int stageIndex_ = 0;
    int tool_ = 1;
    int layer_ = 1;
    int zoom_ = 2;
    int selectedTile_ = 1;
    int selectedPlacement_ = -1;
    int selectedActor_ = 0;
    int selectedSprite_ = 0;
    int selectedSound_ = -1;
    int spriteFrame_ = 0;
    int dragX_ = -1;
    int dragY_ = -1;
    int colorIndex_ = 1;
    bool grid_ = true;
    bool playing_ = false;
    bool stroking_ = false;
    bool draggingActor_ = false;
    bool blocksOpen_ = false;
    int blockActor_ = -1;
    int blockScript_ = 0;
    int blockEdit_ = 0;
    std::vector<int> blockPath_;
    uint8_t paintR_ = 255, paintG_ = 220, paintB_ = 80;
};

}  // namespace cc
