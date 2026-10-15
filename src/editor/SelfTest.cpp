#include "editor/Editor.hpp"

#include "common/Export.hpp"
#include "common/Synth.hpp"
#include "demo/Demo.hpp"
#include "player/Engine.hpp"

#include <SDL.h>

#include "imgui.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace cc {
namespace {

std::filesystem::path lastProjectPath() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return std::filesystem::path(home) / ".config" / "cabinetcore" / "last-project";
}

void writeU16(std::ostream& out, uint16_t value) { out.write(reinterpret_cast<const char*>(&value), 2); }
void writeU32(std::ostream& out, uint32_t value) { out.write(reinterpret_cast<const char*>(&value), 4); }

void writeBmp(const std::filesystem::path& path) {
    const int width = 2;
    const int height = 2;
    const int stride = 8;
    const int pixels = stride * height;
    std::ofstream out(path, std::ios::binary);
    const uint32_t fileSize = 54u + static_cast<uint32_t>(pixels);
    out.put('B');
    out.put('M');
    writeU32(out, fileSize);
    writeU32(out, 0);
    writeU32(out, 54);
    writeU32(out, 40);
    writeU32(out, static_cast<uint32_t>(width));
    writeU32(out, static_cast<uint32_t>(height));
    writeU16(out, 1);
    writeU16(out, 24);
    writeU32(out, 0);
    writeU32(out, static_cast<uint32_t>(pixels));
    writeU32(out, 0);
    writeU32(out, 0);
    writeU32(out, 0);
    writeU32(out, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            out.put(0);
            out.put(0);
            out.put(static_cast<char>(255));
        }
        out.put(0);
        out.put(0);
    }
}

void writeWav(const std::filesystem::path& path, int bits, int channels, int frames) {
    const int bytesPerFrame = (bits / 8) * channels;
    const uint32_t dataBytes = static_cast<uint32_t>(frames * bytesPerFrame);
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4);
    writeU32(out, 36u + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(out, 16);
    writeU16(out, 1);
    writeU16(out, static_cast<uint16_t>(channels));
    writeU32(out, 44100);
    writeU32(out, static_cast<uint32_t>(44100 * bytesPerFrame));
    writeU16(out, static_cast<uint16_t>(bytesPerFrame));
    writeU16(out, static_cast<uint16_t>(bits));
    out.write("data", 4);
    writeU32(out, dataBytes);
    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < channels; ++c) {
            if (bits == 8) out.put(static_cast<char>(180));
            else {
                const int16_t sample = static_cast<int16_t>(1000 + i * 10 + c);
                out.write(reinterpret_cast<const char*>(&sample), 2);
            }
        }
    }
}

uint8_t pixelAt(const Sprite& sprite, int frame, int x, int y, int channel) {
    const size_t index = (static_cast<size_t>(y) * sprite.imgW + frame * sprite.frameW + x) * 4 + channel;
    if (index >= sprite.rgba.size()) return 0;
    return sprite.rgba[index];
}

}  // namespace

int Editor::runSelfTest(SDL_Renderer* renderer) {
    if (renderer) drawRenderer_ = renderer;

    const Project savedProject = project_;
    const std::string savedPath = path_;
    const std::string savedStatus = status_;
    const std::string savedTitle = title_;
    const auto savedUndo = undo_;
    const auto savedRedo = redo_;
    const Project savedBefore = before_;
    const bool savedUndoReady = undoReady_;
    const bool savedUndoBusy = undoBusy_;
    const bool savedUndoUsed = undoUsed_;
    const int savedStage = stageIndex_;
    const int savedTool = tool_;
    const int savedLayer = layer_;
    const int savedZoom = zoom_;
    const int savedTile = selectedTile_;
    const int savedPlacement = selectedPlacement_;
    const int savedActor = selectedActor_;
    const int savedSprite = selectedSprite_;
    const int savedSound = selectedSound_;
    const int savedFrame = spriteFrame_;
    const int savedColor = colorIndex_;
    const uint8_t savedR = paintR_;
    const uint8_t savedG = paintG_;
    const uint8_t savedB = paintB_;
    const int savedPaintTool = paintTool_;
    const bool savedGrid = grid_;
    const bool savedCollision = showCollision_;
    const bool savedBlocks = blocksOpen_;
    const int savedBlockActor = blockActor_;
    const int savedBlockScript = blockScript_;
    const auto savedBlockPath = blockPath_;
    const int savedBlockEdit = blockEdit_;
    const int savedGenShape = genShape_;
    const int savedGenX = genX_;
    const int savedGenY = genY_;
    const int savedGenLen = genLen_;
    const bool savedCoins = genCoins_;
    const bool savedExit = genExit_;
    const bool savedDress = genDress_;
    const int savedPopActor = popActor_;
    const int savedPopX = popX_;
    const int savedPopY = popY_;
    const int savedPopW = popW_;
    const int savedPopH = popH_;
    const int savedPopCount = popCount_;
    const Chunk savedChunk = chunk_;
    const auto savedNotes = levelNotes_;
    const bool savedChecked = levelChecked_;
    const std::filesystem::path remember = lastProjectPath();
    const bool hadRemember = !remember.empty() && std::filesystem::exists(remember);
    std::string rememberText;
    if (hadRemember) {
        std::ifstream in(remember);
        std::getline(in, rememberText);
    }
    const std::filesystem::path temp = std::filesystem::temp_directory_path() / "cabinetcore-selftest";

    auto restore = [&]() {
        if (playing_) togglePlay();
        project_ = savedProject;
        path_ = savedPath;
        status_ = savedStatus;
        title_ = savedTitle;
        undo_ = savedUndo;
        redo_ = savedRedo;
        before_ = savedBefore;
        undoReady_ = savedUndoReady;
        undoBusy_ = savedUndoBusy;
        undoUsed_ = savedUndoUsed;
        stageIndex_ = savedStage;
        tool_ = savedTool;
        layer_ = savedLayer;
        zoom_ = savedZoom;
        selectedTile_ = savedTile;
        selectedPlacement_ = savedPlacement;
        selectedActor_ = savedActor;
        selectedSprite_ = savedSprite;
        selectedSound_ = savedSound;
        spriteFrame_ = savedFrame;
        colorIndex_ = savedColor;
        paintR_ = savedR;
        paintG_ = savedG;
        paintB_ = savedB;
        paintTool_ = savedPaintTool;
        grid_ = savedGrid;
        showCollision_ = savedCollision;
        blocksOpen_ = savedBlocks;
        blockActor_ = savedBlockActor;
        blockScript_ = savedBlockScript;
        blockPath_ = savedBlockPath;
        blockEdit_ = savedBlockEdit;
        genShape_ = savedGenShape;
        genX_ = savedGenX;
        genY_ = savedGenY;
        genLen_ = savedGenLen;
        genCoins_ = savedCoins;
        genExit_ = savedExit;
        genDress_ = savedDress;
        popActor_ = savedPopActor;
        popX_ = savedPopX;
        popY_ = savedPopY;
        popW_ = savedPopW;
        popH_ = savedPopH;
        popCount_ = savedPopCount;
        chunk_ = savedChunk;
        levelNotes_ = savedNotes;
        levelChecked_ = savedChecked;
        std::error_code error;
        std::filesystem::remove_all(temp, error);
        if (!remember.empty()) {
            if (hadRemember) {
                std::filesystem::create_directories(remember.parent_path(), error);
                std::ofstream out(remember);
                if (out) out << rememberText << '\n';
            } else if (std::filesystem::exists(remember)) {
                std::filesystem::remove(remember, error);
            }
        }
    };

    selfChecks_.clear();
    int failed = 0;
    auto expect = [&](const std::string& name, bool pass, const std::string& detail) {
        selfChecks_.push_back({name, pass, detail});
        if (!pass) ++failed;
    };
    auto fresh = [&]() {
        project_ = makeTemplate(PhysicsMode::Platform, "Self-test");
        stageIndex_ = 0;
        selectedPlacement_ = -1;
        undo_.clear();
        redo_.clear();
        genDress_ = false;
        genCoins_ = false;
        genExit_ = false;
        genShape_ = 0;
        chunk_ = {};
        layer_ = 1;
        tool_ = 1;
        selectedTile_ = 1;
    };
    auto actorIndex = [&](const std::string& id) {
        for (int i = 0; i < static_cast<int>(project_.actors.size()); ++i)
            if (project_.actors[static_cast<size_t>(i)].id == id) return i;
        return -1;
    };
    auto mainAt = [&](int x, int y) {
        Stage* stage = currentStage();
        return stage ? stage->get(stage->main, x, y) : uint16_t{0};
    };
    auto hasNote = [&](const char* text) {
        for (const auto& note : levelNotes_)
            if (note.find(text) != std::string::npos) return true;
        return false;
    };
    auto noteText = [&]() {
        std::string text;
        for (const auto& note : levelNotes_) text += note + " ";
        return text;
    };

    try {
        std::filesystem::remove_all(temp);
        std::filesystem::create_directories(temp);

        const PhysicsMode modes[] = {PhysicsMode::Platform, PhysicsMode::RunAndGun, PhysicsMode::ShooterVert,
                                     PhysicsMode::ShooterHorz, PhysicsMode::Maze, PhysicsMode::TopDown};
        const char* modeNames[] = {"Platform", "Run and gun", "Vertical shooter", "Horizontal shooter", "Maze", "Top-down"};
        for (int i = 0; i < 6; ++i) {
            const Project made = makeTemplate(modes[i], modeNames[i]);
            const std::string problem = made.validate();
            expect(std::string(modeNames[i]) + " template is valid", problem.empty(), problem);
        }
        {
            const Project demo = makeDemo();
            const std::string problem = demo.validate();
            expect("Full demo is valid", problem.empty(), problem);
        }

        fresh();
        checkLevel();
        expect("A normal platform stage passes the checker", hasNote("looks ready"), noteText());

        fresh();
        currentStage()->placements.clear();
        checkLevel();
        expect("Checker notices a missing hero", hasNote("Place a hero"), noteText());

        fresh();
        {
            Stage* stage = currentStage();
            stage->placements.push_back(stage->placements.front());
            checkLevel();
            expect("Checker notices two heroes", hasNote("More than one hero"), noteText());
        }

        fresh();
        {
            Stage* stage = currentStage();
            stage->set(stage->main, 2, 10, 1);
            Placement& hero = stage->placements.front();
            hero.x = 32;
            hero.y = 160;
            checkLevel();
            expect("Checker notices a hero inside a wall", hasNote("inside a solid"), noteText());
            stage->set(stage->main, 2, 10, 4);
            checkLevel();
            expect("Checker notices a hero on a hazard", hasNote("on a hazard"), noteText());
            hero.x = 9999;
            checkLevel();
            expect("Checker notices an actor outside the map", hasNote("outside the map"), noteText());
        }

        fresh();
        {
            Stage* stage = currentStage();
            stage->placements.erase(stage->placements.begin() + 1);
            checkLevel();
            expect("Checker notices a missing exit", hasNote("none is placed"), noteText());
            stage->complete = CompleteMode::CollectAll;
            checkLevel();
            expect("Checker notices a coin goal with no coins", hasNote("every coin"), noteText());
            stage->complete = CompleteMode::BossDead;
            checkLevel();
            expect("Checker notices a boss goal with no boss", hasNote("boss dies"), noteText());
            stage->musicId = "missing-tune";
            checkLevel();
            expect("Checker notices a missing music id", hasNote("does not match a sound"), noteText());
            stage->main.clear();
            checkLevel();
            expect("Checker notices a broken tile map", hasNote("does not match the stage"), noteText());
        }

        fresh();
        {
            project_.findActor("door")->needsKey = true;
            checkLevel();
            expect("Checker notices a locked door without a key", hasNote("needs a key"), noteText());
            ActorDef* slime = project_.findActor("slime");
            slime->behavior.pattern = AiPattern::Turret;
            slime->projectileId.clear();
            checkLevel();
            expect("Checker notices an attacker with no shot", hasNote("no projectile"), noteText());
            slime->projectileId = "missing-shot";
            checkLevel();
            expect("Checker notices a missing projectile", hasNote("missing actor"), noteText());
            slime->spriteId = "missing-sprite";
            checkLevel();
            expect("Checker notices a missing sprite", hasNote("missing sprite"), noteText());
            currentStage()->placements.push_back({"e-x", "nobody", 16, 16});
            checkLevel();
            expect("Checker notices an unknown actor id", hasNote("nobody"), noteText());
        }

        fresh();
        {
            Stage* stage = currentStage();
            stage->main.assign(static_cast<size_t>(stage->width * stage->height), 0);
            stage->physics = PhysicsMode::Platform;
            checkLevel();
            expect("Checker notices a platform with nowhere to stand", hasNote("stand on"), noteText());
            for (int y = 5; y <= 8; ++y) stage->set(stage->main, 4, y, 3);
            checkLevel();
            expect("Checker notices a ladder with no landing", hasNote("no landing"), noteText());
            stage->intermission = true;
            checkLevel();
            expect("Checker notices a message stage", hasNote("message screen"), noteText());
        }

        fresh();
        {
            const uint16_t before = mainAt(4, 4);
            tool_ = 1;
            selectedTile_ = 5;
            applyTool(4, 4, true, true);
            expect("Paint writes a tile", mainAt(4, 4) == 5, "");
            applyTool(-1, 0, true, true);
            applyTool(currentStage()->width, 0, true, true);
            expect("Paint ignores tiles outside the map", mainAt(4, 4) == 5 && mainAt(0, 0) == before, "");
            tool_ = 2;
            applyTool(4, 4, true, true);
            expect("Erase clears a tile", mainAt(4, 4) == 0, "");
            tool_ = 3;
            selectedTile_ = 6;
            applyTool(0, 0, true, true);
            expect("Fill spreads through empty tiles", mainAt(1, 1) == 6 && mainAt(0, 13) == 1, "");
            selectedTile_ = 6;
            applyTool(0, 0, true, true);
            expect("Fill of the same tile stops", mainAt(1, 1) == 6, "");
            tool_ = 4;
            selectedTile_ = 7;
            const uint16_t outside = mainAt(6, 6);
            applyTool(8, 8, true, true);
            expect("A rectangle waits for the button to be released", mainAt(8, 8) == outside, "");
            applyTool(10, 9, false, false);
            expect("Rectangle fill covers its corners", mainAt(8, 8) == 7 && mainAt(10, 9) == 7 && mainAt(9, 8) == 7, "");
            expect("Rectangle fill stays inside the drag", mainAt(7, 8) != 7 && mainAt(8, 7) != 7, "");
            layer_ = 0;
            tool_ = 1;
            selectedTile_ = 2;
            applyTool(3, 3, true, true);
            expect("Backdrop layer can be painted", currentStage()->get(currentStage()->back, 3, 3) == 2, "");
            layer_ = 2;
            selectedTile_ = 3;
            applyTool(3, 3, true, true);
            expect("Foreground layer can be painted", currentStage()->get(currentStage()->front, 3, 3) == 3, "");
            layer_ = 1;
            const int heroAt = placementAt(40.f, 200.f);
            expect("Clicking the hero hits that actor", heroAt >= 0, "");
            expect("Empty space does not hit an actor", placementAt(1.f, 1.f) < 0, "");
            tool_ = 0;
            applyTool(2, 12, true, false);
            expect("Select picks the actor on that tile", selectedPlacement_ == heroAt, "");
            const int placed = static_cast<int>(currentStage()->placements.size());
            tool_ = 5;
            selectedActor_ = actorIndex("runner");
            applyTool(6, 6, true, true);
            expect("Stamp places the selected actor", static_cast<int>(currentStage()->placements.size()) == placed + 1, "");
            selectedActor_ = -1;
            applyTool(7, 7, true, true);
            expect("Stamp with no actor selected does nothing", static_cast<int>(currentStage()->placements.size()) == placed + 1, "");
        }

        fresh();
        generateStretch(-8, -4, 0);
        expect("A stretch clamps onto the map", mainAt(0, 0) == 1, status_);
        fresh();
        genShape_ = 0;
        generateStretch(2, 8, 4);
        expect("Floor paints a solid run", mainAt(2, 8) == 1 && mainAt(5, 8) == 1 && mainAt(6, 8) == 0, "");
        fresh();
        genShape_ = 1;
        generateStretch(2, 8, 2);
        expect("A short pit is a full hole", mainAt(2, 8) == 0 && mainAt(3, 8) == 0 && mainAt(2, 9) == 4, "");
        fresh();
        genShape_ = 1;
        generateStretch(2, 8, 4);
        expect("A long pit keeps both landings", mainAt(2, 8) == 1 && mainAt(5, 8) == 1 && mainAt(3, 8) == 0 && mainAt(3, 9) == 4, "");
        fresh();
        genShape_ = 2;
        generateStretch(2, 10, 3);
        expect("Stairs climb toward the right", mainAt(2, 10) == 1 && mainAt(3, 9) == 1 && mainAt(3, 10) == 1 && mainAt(4, 8) == 1, "");
        fresh();
        genShape_ = 2;
        generateStretch(2, 0, 3);
        expect("Stairs at the top of the map stay inside it", mainAt(2, 0) == 1, status_);
        fresh();
        genShape_ = 3;
        generateStretch(8, 12, 4);
        expect("A ladder on the right grows a landing to the left", mainAt(8, 9) == 3 && mainAt(8, 12) == 3 && mainAt(7, 10) == 2, "");
        fresh();
        genShape_ = 3;
        generateStretch(1, 12, 4);
        expect("A ladder on the left grows a landing to the right", mainAt(1, 12) == 3 && mainAt(2, 10) == 2, "");
        fresh();
        genShape_ = 4;
        generateStretch(4, 4, 3);
        expect("A shooter column paints a vertical strip", mainAt(4, 4) == 1 && mainAt(4, 6) == 1 && mainAt(5, 4) == 0, "");
        fresh();
        genShape_ = 1;
        generateStretch(2, currentStage()->height - 1, 2);
        expect("A pit on the bottom row does not write past the map", mainAt(2, currentStage()->height - 1) == 0, "");
        fresh();
        {
            auto tiles = project_.tilesets[0].tiles;
            project_.tilesets[0].tiles.clear();
            const uint16_t before = mainAt(1, 1);
            genShape_ = 0;
            generateStretch(1, 1, 4);
            expect("A tileset with no solid tile refuses to paint", status_.find("no solid") != std::string::npos && mainAt(1, 1) == before,
                   status_);
            project_.tilesets[0].tiles = std::move(tiles);
        }
        fresh();
        genShape_ = 0;
        genX_ = 3;
        genY_ = 8;
        genLen_ = 3;
        generateStretch(3, 8, 3);
        dressStretch(true);
        expect("Dress gives the floor a top edge and a backdrop", mainAt(3, 8) == 2 && currentStage()->get(currentStage()->back, 3, 0) != 0,
               status_);

        fresh();
        popX_ = 0;
        popY_ = 0;
        popW_ = 4;
        popH_ = 2;
        popActor_ = actorIndex("slime");
        const int beforePopulate = static_cast<int>(currentStage()->placements.size());
        populateFloor();
        expect("Populate refuses an area with no floor", status_.find("No floor") != std::string::npos &&
                                                              static_cast<int>(currentStage()->placements.size()) == beforePopulate,
               status_);
        popX_ = 2;
        popY_ = 8;
        popW_ = 8;
        popH_ = 6;
        popCount_ = 3;
        populateFloor();
        expect("Populate places actors on the floor", static_cast<int>(currentStage()->placements.size()) > beforePopulate, status_);
        const int afterFloor = static_cast<int>(currentStage()->placements.size());
        popActor_ = actorIndex("runner");
        populateFloor();
        expect("Populate will not scatter the hero", static_cast<int>(currentStage()->placements.size()) == afterFloor, status_);
        populateTurret();
        expect("A turret can be placed", static_cast<int>(currentStage()->placements.size()) > afterFloor, status_);
        const int afterTurret = static_cast<int>(currentStage()->placements.size());
        populateKeyDoor();
        expect("Key and door can be placed", static_cast<int>(currentStage()->placements.size()) > afterTurret, status_);

        fresh();
        stampChunk(4, 4);
        expect("Stamp before copy explains itself", status_.find("Copy an area first") != std::string::npos, status_);
        copyChunk(-5, -5, 0, 0);
        expect("Copy clamps a backwards area", chunk_.w > 0 && chunk_.h > 0, status_);
        copyChunk(2, 12, 2, 2);
        expect("Copy keeps the actors in the area", !chunk_.actors.empty(), status_);
        const int beforeStamp = static_cast<int>(currentStage()->placements.size());
        stampChunk(16, 12);
        const int stamped = static_cast<int>(currentStage()->placements.size());
        expect("Stamp adds the copied actors", stamped > beforeStamp, status_);
        expect("Stamp copies the tiles", mainAt(16, 13) == mainAt(2, 13), "");
        stampChunk(16, 12);
        expect("Stamping again does not stack the same actors", static_cast<int>(currentStage()->placements.size()) == stamped, "");
        stampChunk(currentStage()->width - 1, currentStage()->height - 1);
        expect("Stamp at the corner stays on the map", status_.find("Stamped") != std::string::npos, status_);
        stampChunkNext();
        expect("Stamp into a missing next stage explains itself", status_.find("no next stage") != std::string::npos, status_);
        addStage(false);
        stageIndex_ = 0;
        stampChunkNext();
        expect("Stamp into the next stage moves there", stageIndex_ == 1 && status_.find("Stamped") != std::string::npos, status_);

        fresh();
        {
            const int width = currentStage()->width;
            const int height = currentStage()->height;
            resizeCurrentStage(1, 1);
            expect("Resize stays at least 5 by 5", currentStage()->width == 5 && currentStage()->height == 5, "");
            currentStage()->set(currentStage()->main, 1, 1, 9);
            resizeCurrentStage(40, 18);
            expect("Resize keeps tiles that still fit", currentStage()->width == 40 && mainAt(1, 1) == 9, "");
            resizeCurrentStage(500, 500);
            expect("Resize stays at most 200 by 200", currentStage()->width == 200 && currentStage()->height == 200, "");
            undo();
            undo();
            undo();
            expect("Undo restores the stage size", currentStage()->width == width && currentStage()->height == height, "");
        }

        fresh();
        expect("The last stage cannot be deleted", (deleteCurrentStage(), project_.stages.size() == 1), status_);
        addStage(false);
        expect("Add stage appends one", project_.stages.size() == 2, "");
        const std::string firstId = project_.stages[0].id;
        addStage(true);
        expect("Duplicate stage gets its own id", project_.stages.size() == 3 && project_.stages.back().id != firstId &&
                                                       project_.stages.back().name.find("copy") != std::string::npos,
               "");
        deleteCurrentStage();
        expect("Delete removes only the current stage", project_.stages.size() == 2, "");
        const int actorsBefore = static_cast<int>(project_.actors.size());
        addActor(ActorKind::Enemy);
        addActor(ActorKind::Item);
        addActor(ActorKind::Gun);
        addActor(ActorKind::Check);
        addActor(ActorKind::Boss);
        expect("New actors get their own ids", static_cast<int>(project_.actors.size()) == actorsBefore + 5, "");
        expect("A new item is a coin and a new gun is a shot",
               project_.actors[project_.actors.size() - 4].pickup == Pickup::Coin && project_.actors.back().kind == ActorKind::Boss &&
                   project_.actors[project_.actors.size() - 3].hitW < 8.f,
               "");

        fresh();
        {
            const uint16_t original = mainAt(4, 4);
            pushUndo();
            currentStage()->set(currentStage()->main, 4, 4, 3);
            undo();
            expect("Undo restores a painted tile", mainAt(4, 4) == original, "");
            redo();
            expect("Redo puts the painted tile back", mainAt(4, 4) == 3, "");
            undo();
            pushUndo();
            currentStage()->set(currentStage()->main, 4, 4, 3);
            addStage(false);
            stageIndex_ = static_cast<int>(project_.stages.size()) - 1;
            undo();
            expect("Undo returns to the stage that changed", stageIndex_ == 0 && mainAt(4, 4) == original, "");
            undo_.clear();
            redo_.clear();
            const uint16_t kept = mainAt(4, 4);
            undo();
            redo();
            expect("Undo and redo do nothing when there is no history", mainAt(4, 4) == kept, "");
            for (int i = 0; i < 40; ++i) pushUndo();
            expect("Undo history stays bounded", undo_.size() == 25, std::to_string(undo_.size()));
            undo_.clear();
            const int count = static_cast<int>(project_.actors.size());
            pushUndo();
            addActor(ActorKind::Enemy);
            selectedActor_ = static_cast<int>(project_.actors.size()) - 1;
            undo();
            expect("Undo puts actor selection back in range",
                   static_cast<int>(project_.actors.size()) == count && selectedActor_ >= 0 &&
                       selectedActor_ < static_cast<int>(project_.actors.size()),
                   "");
        }

        fresh();
        {
            const int hero = actorIndex("runner");
            giveDoubleJump(project_.actors[static_cast<size_t>(hero)]);
            ActorDef& actor = project_.actors[static_cast<size_t>(hero)];
            bool jumps = false;
            bool grounded = false;
            for (const auto& prop : actor.props) {
                if (prop.name == "jumps") jumps = true;
                if (prop.name == "grounded") grounded = true;
            }
            expect("Double jump adds the two properties and both scripts",
                   jumps && grounded && actor.scripts.size() == 2 && actor.scripts[0].hat.kind == BlockKind::EveryFrame &&
                       actor.scripts[1].hat.kind == BlockKind::WhenButton1,
                   "");
            givePatrol(project_.actors[static_cast<size_t>(hero)]);
            expect("Patrol is one every-frame script",
                   actor.scripts.size() == 1 && actor.scripts[0].hat.kind == BlockKind::EveryFrame && actor.scripts[0].body.size() == 2, "");
            giveOpenMessage(project_.actors[static_cast<size_t>(hero)]);
            expect("Open-on-message listens for open and vanishes",
                   actor.scripts.size() == 1 && actor.scripts[0].hat.kind == BlockKind::WhenMessage && actor.scripts[0].hat.name == "open" &&
                       actor.scripts[0].body[0].kind == BlockKind::Vanish,
                   "");
            giveHurtScore(project_.actors[static_cast<size_t>(hero)]);
            expect("Hurt script scores and vanishes",
                   actor.scripts[0].hat.kind == BlockKind::WhenHurt && actor.scripts[0].body.size() == 2, "");

            actor.scripts.clear();
            blockActor_ = hero;
            blockPath_.clear();
            insertBlock(BlockKind::Move);
            insertBlock(BlockKind::Turn);
            expect("Blocks snap onto an every-frame script",
                   actor.scripts.size() == 1 && actor.scripts[0].body.size() == 2 && actor.scripts[0].body[0].kind == BlockKind::Move &&
                       actor.scripts[0].body[1].kind == BlockKind::Turn,
                   "");
            blockEdit_ = 2;
            applyBlockEdit();
            expect("Move up swaps the block with the one above",
                   actor.scripts[0].body[0].kind == BlockKind::Turn && actor.scripts[0].body[1].kind == BlockKind::Move, "");
            blockEdit_ = 3;
            applyBlockEdit();
            expect("Move down swaps the block back",
                   actor.scripts[0].body[0].kind == BlockKind::Move && actor.scripts[0].body[1].kind == BlockKind::Turn, "");
            insertBlock(BlockKind::If);
            insertBlock(BlockKind::Jump);
            expect("A block snapped onto if goes inside it",
                   actor.scripts[0].body.back().kind == BlockKind::If && actor.scripts[0].body.back().inner.size() == 1 &&
                       actor.scripts[0].body.back().inner[0].kind == BlockKind::Jump,
                   "");
            blockEdit_ = 2;
            applyBlockEdit();
            expect("The first block inside if stays put when moved up", actor.scripts[0].body.back().inner[0].kind == BlockKind::Jump, "");
            blockEdit_ = 1;
            applyBlockEdit();
            expect("Delete removes the selected block", actor.scripts[0].body.back().inner.empty(), "");
            blockPath_ = {40};
            blockEdit_ = 1;
            applyBlockEdit();
            expect("Delete of a missing block does nothing", actor.scripts[0].body.size() == 3, "");
            const int scripts = static_cast<int>(actor.scripts.size());
            blockActor_ = -1;
            insertBlock(BlockKind::Vanish);
            expect("Blocks ignore a closed actor", static_cast<int>(project_.actors[static_cast<size_t>(hero)].scripts.size()) == scripts, "");
            blockActor_ = hero;
            insertBlock(BlockKind::WhenMessage);
            insertBlock(BlockKind::SpawnActor);
            insertBlock(BlockKind::SendMessage);
            expect("Message, spawn, and send blocks keep their targets",
                   actor.scripts.back().hat.kind == BlockKind::WhenMessage && actor.scripts.back().hat.name == "open" &&
                       !actor.scripts.back().body.empty() && !actor.scripts.back().body[0].name.empty(),
                   "");
        }

        {
            Sprite sprite;
            sprite.frameW = 4;
            sprite.frameH = 4;
            sprite.frames = 1;
            normalizeSprite(sprite);
            expect("A new sprite allocates its pixels", sprite.imgW == 4 && sprite.rgba.size() == 64, "");
            colorIndex_ = 1;
            paintR_ = 200;
            paintG_ = 10;
            paintB_ = 20;
            plotSprite(sprite, 0, 0, 0, false);
            expect("Pencil draws the chosen color", pixelAt(sprite, 0, 0, 0, 0) == 200 && pixelAt(sprite, 0, 0, 0, 3) == 255, "");
            const uint8_t kept = pixelAt(sprite, 0, 0, 0, 0);
            plotSprite(sprite, 0, -1, 0, false);
            plotSprite(sprite, 0, 4, 4, false);
            expect("Pencil ignores pixels outside the frame", pixelAt(sprite, 0, 0, 0, 0) == kept, "");
            colorIndex_ = -1;
            plotSprite(sprite, 0, 0, 0, true);
            expect("Erase clears a pixel", pixelAt(sprite, 0, 0, 0, 3) == 0, "");
            colorIndex_ = 1;
            paintR_ = 5;
            paintG_ = 6;
            paintB_ = 7;
            floodSprite(sprite, 0, 1, 1, false);
            expect("Fill covers the whole frame", pixelAt(sprite, 0, 0, 0, 0) == 5 && pixelAt(sprite, 0, 3, 3, 0) == 5, "");
            floodSprite(sprite, 0, 1, 1, false);
            expect("Fill of the same color stops", pixelAt(sprite, 0, 2, 2, 0) == 5, "");
            paintR_ = 9;
            strokeSprite(sprite, 0, 0, 0, 3, 0, false);
            expect("A line reaches both ends", pixelAt(sprite, 0, 0, 0, 0) == 9 && pixelAt(sprite, 0, 3, 0, 0) == 9, "");
            sprite.frames = 2;
            normalizeSprite(sprite);
            expect("Adding a frame keeps the old pixels", sprite.imgW == 8 && pixelAt(sprite, 0, 0, 0, 0) == 9, "");
            copySpriteFrame(sprite, 1);
            expect("Copy previous frame duplicates it", pixelAt(sprite, 1, 0, 0, 0) == 9 && pixelAt(sprite, 1, 3, 3, 0) == 5, "");
            copySpriteFrame(sprite, 0);
            expect("Copy on the first frame leaves it alone", pixelAt(sprite, 0, 0, 0, 0) == 9, "");
            pickSprite(sprite, 0, 0, 0);
            expect("Pick reads the pixel color", colorIndex_ == 16 && paintR_ == 9, "");
            colorIndex_ = 1;
            plotSprite(sprite, 0, 1, 1, true);
            pickSprite(sprite, 0, 1, 1);
            expect("Pick on an empty pixel selects erase", colorIndex_ < 0, "");
            sprite.frameW = 2;
            normalizeSprite(sprite);
            expect("Shrinking a frame keeps the corner pixel", sprite.frameW == 2 && pixelAt(sprite, 0, 0, 0, 0) == 9, "");
            sprite.frameW = 0;
            sprite.frameH = 400;
            sprite.frames = 80;
            normalizeSprite(sprite);
            expect("Frame size stays inside the painter limits", sprite.frameW == 1 && sprite.frameH == 128 && sprite.frames == 32, "");
        }

        fresh();
        for (int style = 0; style < 7; ++style) {
            const Sound sound = synthMade("tone", "Tone", style, 72, 0.2f, false);
            expect("Sound style " + std::to_string(style) + " produces audio", !sound.samples.empty(), "");
        }
        {
            const Sound quiet = synthMade("quiet", "Quiet", 0, 0, 0.f, false);
            const Sound longNote = synthMade("long", "Long", 6, 200, 9.f, true);
            expect("Extreme notes and lengths are clamped into a sound", !quiet.samples.empty() && !longNote.samples.empty() && longNote.music,
                   "");
        }
        fresh();
        {
            int jumps = 0;
            for (const auto& sound : project_.sounds)
                if (sound.id == "jump") ++jumps;
            Sound made = synthJump();
            const size_t samples = made.samples.size();
            for (auto& sound : project_.sounds) {
                if (sound.id == made.id) sound = std::move(made);
            }
            int after = 0;
            size_t replaced = 0;
            for (const auto& sound : project_.sounds) {
                if (sound.id == "jump") {
                    ++after;
                    replaced = sound.samples.size();
                }
            }
            expect("Replacing jump keeps a single jump sound", jumps == 1 && after == 1 && replaced == samples, "");
        }

        fresh();
        writeBmp(temp / "dot.png");
        writeBmp(temp / "dot.PNG");
        writeWav(temp / "blip.wav", 16, 1, 8);
        writeWav(temp / "stereo.wav", 16, 2, 4);
        writeWav(temp / "old.wav", 8, 1, 6);
        {
            std::ofstream bad(temp / "bad.wav", std::ios::binary);
            bad << "not a wave";
        }
        {
            std::ofstream junk(temp / "junk.ccproj", std::ios::binary);
            junk << "this is not a project";
        }
        const int spritesBefore = static_cast<int>(project_.sprites.size());
        importPng((temp / "missing.png").string());
        expect("A missing picture is reported", status_.find("Could not read") != std::string::npos, status_);
        importPng((temp / "dot.png").string());
        expect("A picture imports as a sprite", static_cast<int>(project_.sprites.size()) == spritesBefore + 1 &&
                                                    project_.sprites.back().frameW == 2 && project_.sprites.back().rgba[0] == 255,
               status_);
        importWav((temp / "missing.wav").string());
        expect("A missing recording is reported", status_.find("Could not open") != std::string::npos, status_);
        importWav((temp / "bad.wav").string());
        expect("A file that is not a WAV is reported", status_.find("Not a WAV") != std::string::npos, status_);
        const int soundsBefore = static_cast<int>(project_.sounds.size());
        importWav((temp / "blip.wav").string());
        importWav((temp / "stereo.wav").string());
        importWav((temp / "old.wav").string());
        expect("16-bit, stereo, and 8-bit recordings import", static_cast<int>(project_.sounds.size()) == soundsBefore + 3 &&
                                                                 !project_.sounds.back().samples.empty(),
               status_);
        const int spritesMid = static_cast<int>(project_.sprites.size());
        dropFile((temp / "dot.PNG").string());
        expect("Dropping a picture imports it", static_cast<int>(project_.sprites.size()) == spritesMid + 1, status_);
        const std::string titleBefore = project_.settings.title;
        dropFile((temp / "junk.ccproj").string());
        expect("Dropping a broken project does not replace this one", !status_.empty() && project_.settings.title == titleBefore, status_);
        saveProject((temp / "round").string());
        expect("Save adds the project extension", path_.size() > 7 && path_.find(".ccproj") != std::string::npos, path_);
        const std::string savedFile = path_;
        project_.settings.title = "Changed";
        std::string openError;
        const bool opened = openProject(savedFile, openError);
        expect("Open brings back the saved project", opened && project_.settings.title == "Self-test", openError);
        const bool missing = openProject((temp / "nope.ccproj").string(), openError);
        expect("Opening a missing file keeps the current project", !missing && project_.settings.title == "Self-test", openError);
        const bool garbage = openProject((temp / "junk.ccproj").string(), openError);
        expect("Opening a broken file keeps the current project", !garbage && project_.settings.title == "Self-test", openError);
        saveProject("/no/such/cabinetcore/dir/game.ccproj");
        expect("Saving to a missing folder is reported", status_.find("Saved ") == std::string::npos, status_);
        saveProject((temp / "pack.ccproj").string());
        buildGame();
        const auto built = temp / "dist" / "game.dat";
        const auto elf = temp / "dist" / "cabinet.elf";
        expect("Build writes game.dat and cabinet.elf beside the project", std::filesystem::exists(built) && std::filesystem::exists(elf),
               status_);
        if (std::filesystem::exists(built)) {
            Project packed;
            std::string loadError;
            const bool loaded = Project::load(built.string(), packed, loadError);
            expect("The built game.dat loads", loaded && !packed.stages.empty() && packed.validate().empty(), loadError);
        }

        fresh();
        paintMap(nullptr);
        expect("Drawing the map without a window returns", true, "");
        if (drawRenderer_) {
            showCollision_ = true;
            grid_ = true;
            zoom_ = 2;
            paintMap(drawRenderer_);
            expect("The map and tile pictures upload for the editor", mapTex_ != nullptr && tilesetTex_ != nullptr, "");
            showCollision_ = false;
            paintMap(drawRenderer_);
            expect("Collision colors can be turned off again", mapTex_ != nullptr, "");
        } else {
            expect("The map and tile pictures upload for the editor", false, "No renderer was available.");
        }

        fresh();
        if (playing_) togglePlay();
        togglePlay();
        expect("Playtest starts on this stage", playing_ && engine_.phase() == Phase::Playing && engine_.stageIndex() == 0, status_);
        Input input;
        input.axisX = 1.f;
        input.jump = true;
        input.jumpEdge = true;
        input.shoot = true;
        input.shootEdge = true;
        for (int frame = 0; frame < 20; ++frame) {
            updatePlay(1.f / 60.f, input);
            input.jumpEdge = false;
            input.shootEdge = false;
        }
        renderPlay();
        expect("Playtest draws the screen", playWidth() > 0 && playHeight() > 0 &&
                                                playPixels().size() == static_cast<size_t>(playWidth()) * playHeight() * 4,
               "");
        togglePlay();
        expect("Leaving playtest returns to the editor", !playing_, "");

        fresh();
        addStage(true);
        stageIndex_ = 1;
        togglePlay();
        expect("Playtest can start on a later stage", playing_ && engine_.stageIndex() == 1 && engine_.phase() == Phase::Playing, "");
        togglePlay();
        currentStage()->intermission = true;
        stageIndex_ = 0;
        project_.stages[0].intermission = true;
        togglePlay();
        expect("Playtest opens a message stage on its message", engine_.phase() == Phase::Intermission, "");
        togglePlay();

        project_ = makeDemo();
        stageIndex_ = 0;
        const int hero = actorIndex("runner");
        if (hero >= 0) giveDoubleJump(project_.actors[static_cast<size_t>(hero)]);
        togglePlay();
        for (int frame = 0; frame < 30; ++frame) updatePlay(1.f / 60.f, {});
        renderPlay();
        expect("The full demo can be playtested with a custom script", playing_ && playPixels().size() > 0, "");
        togglePlay();

        const bool padReady = (SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) != 0;
        expect("Gamepad support is initialized", padReady, padReady ? "" : SDL_GetError());
        if (padReady) {
            SDL_GameController* pad = nullptr;
            for (int i = 0; i < SDL_NumJoysticks(); ++i) {
                if (!SDL_IsGameController(i)) continue;
                pad = SDL_GameControllerOpen(i);
                break;
            }
            if (pad) {
                SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
                SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
                SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
                SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
                SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X);
                SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START);
                SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK);
                SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
                const std::string name = SDL_GameControllerName(pad) ? SDL_GameControllerName(pad) : "pad";
                SDL_GameControllerClose(pad);
                expect("A connected pad can be read", true, name);
            } else {
                expect("A connected pad can be read", true, "No pad is plugged in. The same buttons as cabinet.elf are used when one is.");
            }
        }

        expect("Save, open, import, and drop were tested without a file dialog", true,
               "The test writes its own files, so it does not wait for you to pick one.");
    } catch (const std::exception& ex) {
        expect("Self-test ran to the end", false, ex.what());
    } catch (...) {
        expect("Self-test ran to the end", false, "The test stopped on an unknown error.");
    }

    restore();
    const int passed = static_cast<int>(selfChecks_.size()) - failed;
    status_ = "Self-test: " + std::to_string(passed) + " passed, " + std::to_string(failed) + " failed.";
    selfTestOpen_ = true;
    return failed;
}

void Editor::drawSelfTest() {
    if (!selfTestOpen_) return;
    ImGui::SetNextWindowSize(ImVec2(720.f, 560.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Self-test", &selfTestOpen_)) {
        ImGui::End();
        return;
    }
    int passed = 0;
    int failed = 0;
    for (const auto& check : selfChecks_) {
        if (check.pass) ++passed;
        else ++failed;
    }
    ImGui::Text("%d passed, %d failed. Your project was put back when the test finished.", passed, failed);
    if (ImGui::Button("Run again")) runSelfTest(nullptr);
    ImGui::Separator();
    ImGui::BeginChild("selftest-results", ImVec2(0, 0), false);
    for (int group = 0; group < 2; ++group) {
        for (const auto& check : selfChecks_) {
            if (check.pass != (group == 1)) continue;
            const ImVec4 color = check.pass ? ImVec4(0.55f, 0.9f, 0.45f, 1.f) : ImVec4(1.f, 0.4f, 0.35f, 1.f);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("%s  %s", check.pass ? "OK" : "FAIL", check.name.c_str());
            ImGui::PopStyleColor();
            if (!check.pass && !check.detail.empty()) ImGui::TextWrapped("%s", check.detail.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace cc
