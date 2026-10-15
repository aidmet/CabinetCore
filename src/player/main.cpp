#include "common/AudioDevice.hpp"
#include "common/Export.hpp"
#include "demo/Demo.hpp"
#include "player/Engine.hpp"

#include <SDL.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void savePpm(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return;
    out << "P6\n" << w << ' ' << h << "\n255\n";
    for (int i = 0; i < w * h; ++i) {
        out.put(static_cast<char>(rgba[i * 4]));
        out.put(static_cast<char>(rgba[i * 4 + 1]));
        out.put(static_cast<char>(rgba[i * 4 + 2]));
    }
}

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

int selfTest(const std::string& playerPath) {
    const cc::Project demo = cc::makeDemo();
    std::string err;
    const std::string roundtrip = "/tmp/cabinetcore_roundtrip.dat";
    if (!demo.save(roundtrip, err)) return fail(err);
    cc::Project loaded;
    if (!cc::Project::load(roundtrip, loaded, err)) return fail(err);
    if (loaded.settings.title != demo.settings.title) return fail("Title did not round-trip");
    if (loaded.stages.size() != demo.stages.size()) return fail("Stage count did not round-trip");
    if (loaded.sprites.empty() || loaded.sounds.empty() || loaded.actors.empty()) return fail("Assets missing after load");
    if (!demo.validate().empty()) return fail(demo.validate());

    cc::Engine title;
    title.start(demo);
    title.render();
    int ink = 0;
    const auto& pixels = title.pixels();
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (pixels[i] > 40 || pixels[i + 1] > 40 || pixels[i + 2] > 40) ink++;
    }
    if (ink < 80) return fail("Title screen did not draw");
    savePpm("/tmp/cabinet_title.ppm", pixels, title.width(), title.height());

    cc::Input start;
    start.startEdge = true;
    title.update(1.f / 60.f, start);
    if (title.phase() != cc::Phase::Playing) return fail("Start did not begin the game");
    const float x0 = title.playerX();
    const float y0 = title.playerY();
    float minY = y0;
    for (int frame = 0; frame < 70; ++frame) {
        cc::Input input;
        input.axisX = 1.f;
        if (frame == 8) input.jumpEdge = true;
        if (frame == 8) input.jump = true;
        if (frame > 8 && frame < 16) input.jump = true;
        if (frame == 30) input.shootEdge = true;
        title.update(1.f / 60.f, input);
        if (!std::isfinite(title.playerX()) || !std::isfinite(title.playerY())) return fail("Player position is not finite");
        minY = std::min(minY, title.playerY());
        if (frame == 36 && title.debugBulletX() < 0.f) return fail("Player gun did not spawn a bullet");
    }
    if (title.playerX() < x0 + 15.f) return fail("Player did not run to the right");
    if (minY > y0 - 4.f) return fail("Jump did not leave the ground");
    if (title.hp() <= 0) return fail("Player died during the opening run");
    title.render();
    savePpm("/tmp/cabinet_play.ppm", title.pixels(), title.width(), title.height());

    int mazeIndex = -1;
    for (int i = 0; i < static_cast<int>(demo.stages.size()); ++i) {
        if (demo.stages[i].physics == cc::PhysicsMode::Maze) mazeIndex = i;
    }
    if (mazeIndex < 0) return fail("Demo is missing a maze stage");
    cc::Engine maze;
    maze.start(demo, mazeIndex);
    if (maze.phase() != cc::Phase::Playing) return fail("Maze stage did not start");
    for (int frame = 0; frame < 150; ++frame) {
        cc::Input input;
        input.axisX = (frame / 40) % 2 == 0 ? 1.f : 0.f;
        input.axisY = (frame / 40) % 2 == 0 ? 0.f : -1.f;
        maze.update(1.f / 60.f, input);
        const float cx = maze.playerX() + 8.f;
        const float cy = maze.playerY() + 8.f;
        if (maze.debugBlocked(cx, cy)) return fail("Maze player walked into a wall");
    }
    maze.render();
    savePpm("/tmp/cabinet_maze.ppm", maze.pixels(), maze.width(), maze.height());

    cc::Project blocks = cc::makeTemplate(cc::PhysicsMode::Platform, "Blocks");
    cc::ActorDef* runner = nullptr;
    cc::ActorDef* slime = nullptr;
    for (auto& actor : blocks.actors) {
        if (actor.id == "runner") runner = &actor;
        if (actor.id == "slime") slime = &actor;
    }
    if (!runner || !slime) return fail("Template is missing the hero or slime");
    runner->props.push_back({"speed", 36.f});
    cc::Script scoreScript;
    scoreScript.hat.kind = cc::BlockKind::WhenButton2;
    cc::Block points;
    points.kind = cc::BlockKind::AddScore;
    points.num = 40.f;
    scoreScript.body.push_back(points);
    runner->scripts.push_back(scoreScript);
    slime->props.push_back({"ticks", 0.f});
    cc::Script tickScript;
    tickScript.hat.kind = cc::BlockKind::EveryFrame;
    cc::Block tick;
    tick.kind = cc::BlockKind::ChangeProp;
    tick.name = "ticks";
    tick.num = 1.f;
    tickScript.body.push_back(tick);
    slime->scripts.push_back(tickScript);
    if (!blocks.save("/tmp/cabinet_blocks.dat", err)) return fail(err);
    cc::Project loadedBlocks;
    if (!cc::Project::load("/tmp/cabinet_blocks.dat", loadedBlocks, err)) return fail(err);
    const cc::ActorDef* loadedHero = loadedBlocks.findActor("runner");
    if (!loadedHero || loadedHero->props.size() != 1 || loadedHero->props[0].name != "speed" ||
        loadedHero->scripts.size() != 1 || loadedHero->scripts[0].body.size() != 1 ||
        loadedHero->scripts[0].body[0].kind != cc::BlockKind::AddScore) {
        return fail("Block scripts did not round-trip");
    }
    cc::Engine scripted;
    scripted.start(blocks);
    cc::Input begin;
    begin.startEdge = true;
    scripted.update(1.f / 60.f, begin);
    const float scriptX = scripted.playerX();
    for (int frame = 0; frame < 60; ++frame) {
        cc::Input input;
        input.axisX = 1.f;
        scripted.update(1.f / 60.f, input);
    }
    const float moved = scripted.playerX() - scriptX;
    if (moved < 20.f || moved > 55.f) return fail("Hero speed property did not drive movement");
    if (scripted.debugProp("ticks") < 55.f) return fail("Actor script did not update its property");
    cc::Input shoot;
    shoot.shootEdge = true;
    scripted.update(1.f / 60.f, shoot);
    if (scripted.score() != 40) return fail("Button script did not add score");

    const std::string outDir = "/tmp/cabinetcore_export";
    if (!cc::exportGame(demo, outDir, playerPath, err)) return fail(err);
    if (!std::filesystem::exists(outDir + "/game.dat")) return fail("game.dat was not written");
    if (!std::filesystem::exists(outDir + "/cabinet.elf")) return fail("cabinet.elf was not written");
    std::cout << "selftest ok\n";
    return 0;
}

struct KeyEdge {
    bool jump = false;
    bool shoot = false;
    bool start = false;
    bool pause = false;
    bool quit = false;
    bool restart = false;
};

cc::Input pollInput(const Uint8* keys, SDL_GameController* pad, KeyEdge& prev) {
    cc::Input input;
    const bool left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
    const bool right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
    const bool up = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
    const bool down = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
    if (left) input.axisX -= 1.f;
    if (right) input.axisX += 1.f;
    if (up) input.axisY -= 1.f;
    if (down) input.axisY += 1.f;
    bool jump = keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_SPACE];
    bool shoot = keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_LCTRL];
    bool start = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER];
    bool pause = keys[SDL_SCANCODE_ESCAPE];
    bool quit = keys[SDL_SCANCODE_Q];
    bool restart = keys[SDL_SCANCODE_R];
    if (pad) {
        const int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        const int ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (std::abs(ax) > 8000) input.axisX = ax / 32767.f;
        if (std::abs(ay) > 8000) input.axisY = ay / 32767.f;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) input.axisX = -1.f;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) input.axisX = 1.f;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) input.axisY = -1.f;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) input.axisY = 1.f;
        jump = jump || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
        shoot = shoot || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X) ||
                SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
        start = start || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START);
        pause = pause || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK);
    }
    input.jump = jump;
    input.shoot = shoot;
    input.start = start;
    input.pause = pause;
    input.jumpEdge = jump && !prev.jump;
    input.shootEdge = shoot && !prev.shoot;
    input.startEdge = start && !prev.start;
    input.pauseEdge = pause && !prev.pause;
    input.quitEdge = quit && !prev.quit;
    input.restartEdge = restart && !prev.restart;
    prev = {jump, shoot, start, pause, quit, restart};
    return input;
}

std::string findGame(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--", 0) == 0) continue;
        return arg;
    }
    if (std::filesystem::exists("game.dat")) return "game.dat";
    if (argc > 0) {
        const std::filesystem::path beside = std::filesystem::path(argv[0]).parent_path() / "game.dat";
        if (std::filesystem::exists(beside)) return beside.string();
    }
    return "game.dat";
}

int play(const std::string& path) {
    cc::Project project;
    std::string err;
    if (!cc::Project::load(path, project, err)) return fail(err);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) return fail(SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    const int scale = std::max(1, std::min(6, project.settings.scale));
    SDL_Window* window = SDL_CreateWindow(project.settings.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          project.settings.viewW * scale, project.settings.viewH * scale,
                                          SDL_WINDOW_RESIZABLE);
    if (!window) return fail(SDL_GetError());
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) return fail(SDL_GetError());
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                             project.settings.viewW, project.settings.viewH);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);

    std::filesystem::path scorePath = std::filesystem::path(path).parent_path() / "scores.dat";
    if (scorePath.parent_path().empty()) scorePath = "scores.dat";
    cc::Engine engine;
    engine.start(project, 0, scorePath.string());
    cc::AudioDevice audio;
    audio.open();

    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            break;
        }
    }

    KeyEdge edges;
    bool running = true;
    uint64_t last = SDL_GetPerformanceCounter();
    const double tick = static_cast<double>(SDL_GetPerformanceFrequency());
    double acc = 0;
    while (running && !engine.quitRequested()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_CONTROLLERDEVICEADDED && !pad && SDL_IsGameController(event.cdevice.which))
                pad = SDL_GameControllerOpen(event.cdevice.which);
        }
        const uint64_t now = SDL_GetPerformanceCounter();
        acc += (static_cast<double>(now - last) / tick);
        last = now;
        if (acc > 0.25) acc = 0.25;
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        while (acc >= 1.0 / 60.0) {
            const cc::Input input = pollInput(keys, pad, edges);
            engine.update(1.f / 60.f, input);
            acc -= 1.0 / 60.0;
        }
        for (const std::string& id : engine.takeSounds()) audio.playId(project, id, 0.8f, false);
        std::string music;
        if (engine.takeMusic(music)) audio.playId(project, music, 0.45f, true);

        engine.render();
        SDL_UpdateTexture(texture, nullptr, engine.pixels().data(), project.settings.viewW * 4);
        int winW = 0, winH = 0;
        SDL_GetRendererOutputSize(renderer, &winW, &winH);
        int fit = std::min(winW / project.settings.viewW, winH / project.settings.viewH);
        if (fit < 1) fit = 1;
        const SDL_Rect dest{ (winW - project.settings.viewW * fit) / 2, (winH - project.settings.viewH * fit) / 2,
                             project.settings.viewW * fit, project.settings.viewH * fit };
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, &dest);
        if (project.settings.crt && fit >= 2) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 80);
            for (int row = 0; row < project.settings.viewH; ++row) {
                const int y = dest.y + row * fit + fit - 1;
                SDL_RenderDrawLine(renderer, dest.x, y, dest.x + dest.w, y);
            }
        }
        SDL_RenderPresent(renderer);
    }
    if (pad) SDL_GameControllerClose(pad);
    audio.close();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--selftest") return selfTest(argc > 0 ? argv[0] : "cabinet.elf");
        if (arg == "--sim") {
            if (i + 1 >= argc) return fail("Usage: cabinet.elf --sim game.dat [--frames N]");
            const std::string path = argv[++i];
            int frames = 300;
            if (i + 2 < argc && std::string(argv[i + 1]) == "--frames") frames = std::atoi(argv[i + 2]);
            cc::Project project;
            std::string err;
            if (!cc::Project::load(path, project, err)) return fail(err);
            cc::Engine engine;
            engine.start(project);
            cc::Input start;
            start.startEdge = true;
            engine.update(1.f / 60.f, start);
            for (int frame = 0; frame < frames; ++frame) {
                cc::Input input;
                input.axisX = 1.f;
                if (frame % 45 == 0) input.jumpEdge = true;
                if (frame % 12 == 0) input.shootEdge = true;
                engine.update(1.f / 60.f, input);
            }
            std::cout << "sim ok score=" << engine.score() << " stage=" << engine.stageIndex()
                      << " x=" << engine.playerX() << " y=" << engine.playerY() << '\n';
            return 0;
        }
    }
    return play(findGame(argc, argv));
}
