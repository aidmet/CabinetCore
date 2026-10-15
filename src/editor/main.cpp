#include "editor/Editor.hpp"

#include "cabinet_player_path.h"
#include "common/Export.hpp"
#include "demo/Demo.hpp"

#include <SDL.h>

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void applyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.f;
    style.FrameRounding = 3.f;
    style.GrabRounding = 3.f;
    style.ScrollbarRounding = 3.f;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.08f, 0.12f, 1.f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.10f, 0.15f, 1.f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.14f, 0.20f, 1.f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.18f, 0.16f, 1.f);
    colors[ImGuiCol_Button] = ImVec4(0.28f, 0.18f, 0.12f, 1.f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.85f, 0.48f, 0.14f, 1.f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.95f, 0.62f, 0.18f, 1.f);
    colors[ImGuiCol_Header] = ImVec4(0.36f, 0.22f, 0.12f, 1.f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.75f, 0.42f, 0.14f, 1.f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.90f, 0.55f, 0.16f, 1.f);
    colors[ImGuiCol_Tab] = ImVec4(0.20f, 0.14f, 0.12f, 1.f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.85f, 0.48f, 0.14f, 1.f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.55f, 0.30f, 0.12f, 1.f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.08f, 0.10f, 1.f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.45f, 0.24f, 0.10f, 1.f);
    colors[ImGuiCol_CheckMark] = ImVec4(1.f, 0.78f, 0.28f, 1.f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.62f, 0.18f, 1.f);
}

cc::Input pollPlay(const Uint8* keys, SDL_GameController* pad, bool& prevJump, bool& prevShoot, bool& prevStart,
                   bool& prevPause, bool& prevQuit, bool& prevRestart) {
    cc::Input input;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) input.axisX -= 1.f;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) input.axisX += 1.f;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) input.axisY -= 1.f;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) input.axisY += 1.f;
    bool jump = keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_SPACE];
    bool shoot = keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_LCTRL];
    bool start = keys[SDL_SCANCODE_RETURN];
    bool pause = keys[SDL_SCANCODE_ESCAPE];
    const bool quit = keys[SDL_SCANCODE_Q];
    const bool restart = keys[SDL_SCANCODE_R];
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
    input.jumpEdge = jump && !prevJump;
    input.shootEdge = shoot && !prevShoot;
    input.startEdge = start && !prevStart;
    input.pauseEdge = pause && !prevPause;
    input.quitEdge = quit && !prevQuit;
    input.restartEdge = restart && !prevRestart;
    prevJump = jump;
    prevShoot = shoot;
    prevStart = start;
    prevPause = pause;
    prevQuit = quit;
    prevRestart = restart;
    return input;
}

void presentPlay(SDL_Renderer* renderer, SDL_Texture*& texture, int texW, int texH, const cc::Editor& editor) {
    if (!texture || texW != editor.playWidth() || texH != editor.playHeight()) {
        if (texture) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, editor.playWidth(),
                                    editor.playHeight());
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    }
    SDL_UpdateTexture(texture, nullptr, editor.playPixels().data(), editor.playWidth() * 4);
    int winW = 0, winH = 0;
    SDL_GetRendererOutputSize(renderer, &winW, &winH);
    int fit = std::min(winW / editor.playWidth(), winH / editor.playHeight());
    if (fit < 1) fit = 1;
    const SDL_Rect dest{(winW - editor.playWidth() * fit) / 2, (winH - editor.playHeight() * fit) / 2,
                        editor.playWidth() * fit, editor.playHeight() * fit};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, &dest);
    if (editor.playCrt() && fit >= 2) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 70);
        for (int row = 0; row < editor.playHeight(); ++row) {
            const int y = dest.y + row * fit + fit - 1;
            SDL_RenderDrawLine(renderer, dest.x, y, dest.x + dest.w, y);
        }
    }
    SDL_RenderPresent(renderer);
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--export-demo" && i + 1 < argc) {
            const std::string dir = argv[i + 1];
            const cc::Project demo = cc::makeDemo();
            std::string err;
            if (!cc::exportGame(demo, dir, CABINET_PLAYER_PATH, err)) {
                std::cerr << err << '\n';
                return 1;
            }
            if (!demo.save((std::filesystem::path(dir) / "neon.ccproj").string(), err)) {
                std::cerr << err << '\n';
                return 1;
            }
            std::cout << "Exported " << dir << "/game.dat and cabinet.elf\n";
            return 0;
        }
    }

    std::string smokePath;
    std::string openPath;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke" && i + 1 < argc) smokePath = argv[++i];
        else if (arg.rfind("--", 0) != 0) openPath = arg;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window* window = SDL_CreateWindow("CabinetCore", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1480, 900,
                                          SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().FontGlobalScale = 1.15f;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    applyStyle();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    cc::Editor editor;
    if (!openPath.empty()) {
        std::string err;
        if (!editor.openProject(openPath, err)) std::cerr << err << '\n';
    }

    SDL_Texture* playTex = nullptr;
    int playW = 0, playH = 0;
    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            break;
        }
    }
    bool prevF5 = false;
    bool prevJump = false, prevShoot = false, prevStart = false, prevPause = false, prevQuit = false, prevRestart = false;
    bool running = true;
    int frames = 0;
    uint64_t last = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    double acc = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!editor.playing()) ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_CONTROLLERDEVICEADDED && !pad && SDL_IsGameController(event.cdevice.which))
                pad = SDL_GameControllerOpen(event.cdevice.which);
            if (event.type == SDL_CONTROLLERDEVICEREMOVED && pad &&
                SDL_GameControllerFromInstanceID(event.cdevice.which) == pad) {
                SDL_GameControllerClose(pad);
                pad = nullptr;
            }
            if (event.type == SDL_DROPFILE) {
                editor.dropFile(event.drop.file);
                SDL_free(event.drop.file);
            }
        }
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const bool f5 = keys[SDL_SCANCODE_F5] != 0;
        if (f5 && !prevF5) editor.togglePlay();
        prevF5 = f5;

        const uint64_t now = SDL_GetPerformanceCounter();
        acc += static_cast<double>(now - last) / freq;
        last = now;
        if (acc > 0.25) acc = 0.25;

        if (editor.playing()) {
            while (acc >= 1.0 / 60.0) {
                editor.updatePlay(1.f / 60.f,
                                  pollPlay(keys, pad, prevJump, prevShoot, prevStart, prevPause, prevQuit, prevRestart));
                acc -= 1.0 / 60.0;
            }
            editor.renderPlay();
            presentPlay(renderer, playTex, playW, playH, editor);
            playW = editor.playWidth();
            playH = editor.playHeight();
        } else {
            acc = 0;
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            SDL_SetWindowTitle(window, editor.windowTitle().c_str());
            editor.draw(renderer);
            ImGui::Render();
            SDL_SetRenderDrawColor(renderer, 12, 10, 16, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        }

        if (!smokePath.empty() && ++frames >= 2) {
            int w = 0, h = 0;
            SDL_GetRendererOutputSize(renderer, &w, &h);
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
            SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, surface->pixels, surface->pitch);
            std::ofstream out(smokePath, std::ios::binary);
            out << "P6\n" << w << ' ' << h << "\n255\n";
            auto* pixels = static_cast<unsigned char*>(surface->pixels);
            for (int y = 0; y < h; ++y) {
                unsigned char* row = pixels + y * surface->pitch;
                for (int x = 0; x < w; ++x) {
                    out.put(static_cast<char>(row[x * 4]));
                    out.put(static_cast<char>(row[x * 4 + 1]));
                    out.put(static_cast<char>(row[x * 4 + 2]));
                }
            }
            SDL_FreeSurface(surface);
            running = false;
        }
    }

    if (playTex) SDL_DestroyTexture(playTex);
    if (pad) SDL_GameControllerClose(pad);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
