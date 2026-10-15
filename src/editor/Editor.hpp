#pragma once

#include "common/AudioDevice.hpp"
#include "common/Canvas.hpp"
#include "common/Types.hpp"
#include "player/Engine.hpp"
#include "plugin/Plugin.hpp"

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
    enum class SelfTestKind { Editor = 0, Gameplay = 1, Ui = 2 };

    struct SelfCheck {
        std::string name;
        bool pass = false;
        std::string detail;
        SelfTestKind kind = SelfTestKind::Editor;
    };

    bool playing() const { return playing_; }
    int runSelfTest(SDL_Renderer* renderer = nullptr, int which = -1);
    const std::vector<SelfCheck>& selfChecks() const { return selfChecks_; }
    void togglePlay();
    void toggleHitboxes();
    void updatePlay(float dt, const Input& input);
    void renderPlay();
    const std::vector<uint8_t>& playPixels() const;
    int playWidth() const;
    int playHeight() const;
    bool playCrt() const;
    const std::string& windowTitle() const;

private:
    struct Snapshot {
        Project project;
        int stageIndex = 0;
    };

    Stage* currentStage();
    const Stage* currentStage() const;
    std::vector<uint16_t>& activeLayer();
    void ensureLayers(Stage& stage);
    void pushUndo();
    void pushState(const Project& state);
    void restore(const Snapshot& snap);
    void trackUndo();
    void undo();
    void redo();
    void checkLevel();
    void paintMap(SDL_Renderer* renderer);
    void drawMenu();
    void drawToolbar();
    void drawLeft();
    void drawBuildTab();
    void drawCanvas();
    void drawInspector();
    void drawBottom();
    void drawStageSettings(Stage& stage);
    void drawActorEditor(ActorDef& actor);
    void drawBlockEditor();
    void drawTileFlags();
    void applyTool(int tx, int ty, bool click, bool down);
    int placementAt(float x, float y) const;
    uint16_t findTile(uint32_t need, uint32_t avoid);
    bool standable(const Stage& stage, int x, int y);
    bool placeActor(Stage& stage, const ActorDef* actor, int tx, int ty);
    void generateStretch(int x, int y, int length);
    void populateFloor();
    void populateTurret();
    void populateKeyDoor();
    void finishGeneratorDrag();
    void giveDoubleJump(ActorDef& actor);
    void givePatrol(ActorDef& actor);
    void giveOpenMessage(ActorDef& actor);
    void giveHurtScore(ActorDef& actor);
    void dressStretch(bool recordUndo);
    void copyChunk(int x, int y, int w, int h);
    void stampChunk(int x, int y);
    void stampChunkNext();
    void flood(int tx, int ty, uint16_t from, uint16_t to);
    void saveProject(const std::string& path);
    void buildGame();
    void importPng(const std::string& path);
    void importWav(const std::string& path);
    enum class FilePick { None, OpenProject, SaveProject, LoadPlugin, ImportPng, ImportWav };
    struct FileEntry {
        std::string name;
        bool directory = false;
    };
    void openFilePick(FilePick kind);
    void drawFilePick();
    void confirmFilePick();
    void acceptFilePick(const std::string& path);
    std::string filePickPath() const;
    const char* filePickExtension() const;
    std::vector<FileEntry> fileEntries(const std::string& directory, const std::string& extension) const;
    void addActor(ActorKind kind);
    void addStage(bool duplicate);
    void deleteCurrentStage();
    void resizeCurrentStage(int width, int height);
    void insertBlock(BlockKind kind);
    void insertPluginBlock(const std::string& name);
    void insertPrepared(Block block);
    void loadEditorPlugin(const std::string& path);
    void fillPluginHost(PluginHost& host, int tx, int ty);
    void runPluginTool(int tx, int ty);
    void runPluginGenerator(const std::string& name);
    void applyBlockEdit();
    void normalizeSprite(Sprite& sprite);
    void plotSprite(Sprite& sprite, int frame, int x, int y, bool erase);
    void floodSprite(Sprite& sprite, int frame, int x, int y, bool erase);
    void strokeSprite(Sprite& sprite, int frame, int x0, int y0, int x1, int y1, bool erase);
    void copySpriteFrame(Sprite& sprite, int frame);
    void pickSprite(Sprite& sprite, int frame, int x, int y);
    void plotSheet(std::vector<uint8_t>& rgba, int imgW, int ox, int oy, int w, int h, int x, int y, bool erase);
    void floodSheet(std::vector<uint8_t>& rgba, int imgW, int ox, int oy, int w, int h, int x, int y, bool erase);
    void strokeSheet(std::vector<uint8_t>& rgba, int imgW, int ox, int oy, int w, int h, int x0, int y0, int x1, int y1,
                     bool erase);
    void pickSheet(std::vector<uint8_t>& rgba, int imgW, int ox, int oy, int w, int h, int x, int y);
    void drawSheetPainter(std::vector<uint8_t>& rgba, int imgW, int ox, int oy, int w, int h);
    void buildStage();
    void drawSelfTest();
    void zoomIn();
    void zoomOut();
    void zoomWheel(float wheel);
    void loadLayout();
    void saveLayout();
    void resetLayout();
    float snapCoord(float value, int tile) const;
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
    Project before_;
    bool undoReady_ = false;
    bool undoBusy_ = false;
    bool undoUsed_ = false;
    bool undoSuspend_ = false;
    std::vector<std::string> levelNotes_;
    bool levelChecked_ = false;
    bool showCollision_ = false;
    bool showHitboxes_ = false;
    int paintTarget_ = 0;
    std::vector<SelfCheck> selfChecks_;
    bool selfTestOpen_ = false;
    int selfTestWhich_ = -1;
    float uiLeftW_ = 0;
    float uiLeftH_ = 0;
    float uiCanvasW_ = 0;
    float uiCanvasH_ = 0;
    float uiInspectorW_ = 0;
    float uiBottomH_ = 0;
    float layoutLeft_ = 320.f;
    float layoutInspector_ = 340.f;
    float layoutBottom_ = 64.f;
    float layoutBottomPaint_ = -1.f;
    SDL_Renderer* drawRenderer_ = nullptr;
    SDL_Texture* mapTex_ = nullptr;
    SDL_Texture* tilesetTex_ = nullptr;
    int tilesetTexW_ = 0;
    int tilesetTexH_ = 0;
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
    int paintTool_ = 0;
    int paintZoom_ = 12;
    bool paintLine_ = false;
    int paintX0_ = -1;
    int paintY0_ = -1;
    int soundStyle_ = 0;
    int soundNote_ = 72;
    float soundSeconds_ = 0.25f;
    bool soundMusic_ = false;
    std::string soundName_ = "Blip";
    bool grid_ = true;
    bool snapActors_ = false;
    bool playing_ = false;
    std::string armedPlugin_;
    FilePick filePick_ = FilePick::None;
    bool filePickOpen_ = false;
    bool filePickRequest_ = false;
    bool fileReplace_ = false;
    bool fileDialogDrawn_ = false;
    std::string fileDir_;
    std::string fileName_;
    std::string fileNote_;
    bool stroking_ = false;
    bool draggingActor_ = false;
    bool blocksOpen_ = false;
    int blockActor_ = -1;
    int blockScript_ = 0;
    int blockEdit_ = 0;
    std::vector<int> blockPath_;
    int genShape_ = 0;
    int genX_ = 2;
    int genY_ = 12;
    int genLen_ = 8;
    int genDrag_ = 0;
    int genX0_ = 0;
    int genY0_ = 0;
    int genX1_ = 0;
    int genY1_ = 0;
    int popX_ = 2;
    int popY_ = 8;
    int popW_ = 10;
    int popH_ = 6;
    int popCount_ = 4;
    int popActor_ = -1;
    bool genCoins_ = true;
    bool genExit_ = false;
    bool genDown_ = false;
    bool genDress_ = true;
    int stampX_ = 2;
    int stampY_ = 2;
    struct ClipActor {
        int dx = 0;
        int dy = 0;
        int ox = 0;
        int oy = 0;
        std::string actorId;
    };
    struct Chunk {
        int w = 0;
        int h = 0;
        std::vector<uint16_t> main;
        std::vector<uint16_t> back;
        std::vector<uint16_t> front;
        std::vector<ClipActor> actors;
    };
    Chunk chunk_;
    uint8_t paintR_ = 255, paintG_ = 220, paintB_ = 80;
};

}  // namespace cc
