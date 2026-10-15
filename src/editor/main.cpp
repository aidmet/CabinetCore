#include "editor/Editor.hpp"

#include "cabinet_player_path.h"
#include "common/Export.hpp"
#include "demo/Demo.hpp"

#include <SDL.h>

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void applyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.f;
    style.FrameRounding = 3.f;
    style.GrabRounding = 3.f;
    style.ScrollbarRounding = 3.f;
    style.FramePadding = ImVec2(6.f, 3.f);
    style.ItemSpacing = ImVec2(8.f, 5.f);
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
                   bool& prevPause, bool& prevQuit, bool& prevRestart, bool& prevCoin) {
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
    const bool coin = keys[SDL_SCANCODE_5];
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
    input.coinEdge = coin && !prevCoin;
    prevJump = jump;
    prevShoot = shoot;
    prevStart = start;
    prevPause = pause;
    prevQuit = quit;
    prevRestart = restart;
    prevCoin = coin;
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

uint32_t pngCrc(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

void pngChunk(std::ostream& out, const char* type, const uint8_t* data, size_t size) {
    const uint32_t length = static_cast<uint32_t>(size);
    const uint8_t head[4] = {static_cast<uint8_t>(length >> 24), static_cast<uint8_t>(length >> 16),
                             static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)};
    out.write(reinterpret_cast<const char*>(head), 4);
    out.write(type, 4);
    if (size > 0) out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    std::vector<uint8_t> hashed(4 + size);
    std::memcpy(hashed.data(), type, 4);
    if (size > 0) std::memcpy(hashed.data() + 4, data, size);
    const uint32_t crc = pngCrc(hashed.data(), hashed.size());
    const uint8_t tail[4] = {static_cast<uint8_t>(crc >> 24), static_cast<uint8_t>(crc >> 16), static_cast<uint8_t>(crc >> 8),
                             static_cast<uint8_t>(crc)};
    out.write(reinterpret_cast<const char*>(tail), 4);
}

bool savePng(const std::filesystem::path& path, const uint8_t* pixels, int width, int height, int pitch) {
    if (!pixels || width <= 0 || height <= 0) return false;
    std::vector<uint8_t> raw(static_cast<size_t>(height) * (static_cast<size_t>(width) * 4 + 1));
    for (int y = 0; y < height; ++y) {
        uint8_t* row = raw.data() + static_cast<size_t>(y) * (static_cast<size_t>(width) * 4 + 1);
        row[0] = 0;
        std::memcpy(row + 1, pixels + static_cast<size_t>(y) * pitch, static_cast<size_t>(width) * 4);
    }

    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    uint32_t s1 = 1;
    uint32_t s2 = 0;
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t chunk = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + chunk == raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(chunk & 255));
        zlib.push_back(static_cast<uint8_t>((chunk >> 8) & 255));
        const uint16_t negated = static_cast<uint16_t>(~static_cast<uint16_t>(chunk));
        zlib.push_back(static_cast<uint8_t>(negated & 255));
        zlib.push_back(static_cast<uint8_t>((negated >> 8) & 255));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + chunk));
        for (size_t i = 0; i < chunk; ++i) {
            s1 = (s1 + raw[pos + i]) % 65521u;
            s2 = (s2 + s1) % 65521u;
        }
        pos += chunk;
    }
    const uint32_t adler = (s2 << 16) | s1;
    zlib.push_back(static_cast<uint8_t>(adler >> 24));
    zlib.push_back(static_cast<uint8_t>(adler >> 16));
    zlib.push_back(static_cast<uint8_t>(adler >> 8));
    zlib.push_back(static_cast<uint8_t>(adler));

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.write(reinterpret_cast<const char*>(sig), 8);
    const uint8_t ihdr[13] = {static_cast<uint8_t>(width >> 24), static_cast<uint8_t>(width >> 16), static_cast<uint8_t>(width >> 8),
                              static_cast<uint8_t>(width),       static_cast<uint8_t>(height >> 24), static_cast<uint8_t>(height >> 16),
                              static_cast<uint8_t>(height >> 8), static_cast<uint8_t>(height),      8,
                              6,                                 0,                                  0,
                              0};
    pngChunk(out, "IHDR", ihdr, 13);
    pngChunk(out, "IDAT", zlib.data(), zlib.size());
    pngChunk(out, "IEND", nullptr, 0);
    return static_cast<bool>(out);
}

bool captureFrame(SDL_Renderer* renderer, const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return false;
    if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
    const int read = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, surface->pixels, surface->pitch);
    const bool saved = read == 0 && savePng(path, static_cast<uint8_t*>(surface->pixels), width, height, surface->pitch);
    if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
    SDL_FreeSurface(surface);
    return saved;
}

void drawEditorFrame(SDL_Window* window, SDL_Renderer* renderer, cc::Editor& editor, bool closePopups) {
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (closePopups) ImGui::ClosePopupsExceptModals();
    SDL_SetWindowTitle(window, editor.windowTitle().c_str());
    editor.draw(renderer);
    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 12, 10, 16, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
}

int captureShowcase(SDL_Window* window, SDL_Renderer* renderer, cc::Editor& editor, const std::string& directory) {
    namespace fs = std::filesystem;
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) {
        std::cerr << "Could not create " << directory << '\n';
        return 1;
    }
    struct Shot {
        int which;
        const char* name;
        bool play;
    };
    const Shot shots[] = {
        {0, "Rooftops stage.png", false},
        {1, "File menu.png", false},
        {2, "Actor inspector.png", false},
        {3, "Pixel painter.png", false},
        {4, "Block editor.png", false},
        {5, "Rooftops playtest.png", true},
        {6, "Star Raid stage.png", false},
        {7, "Star Raid playtest.png", true},
        {8, "Pellet Pit stage.png", false},
        {9, "Pellet Pit playtest.png", true},
        {10, "Get Ready.png", true},
    };
    for (const Shot& shot : shots) {
        editor.prepareShowcase(shot.which);
        const fs::path path = fs::path(directory) / shot.name;
        if (shot.play) {
            editor.togglePlay();
            SDL_Texture* texture = nullptr;
            int texW = 0;
            int texH = 0;
            const int frames = shot.which == 10 ? 8 : (shot.which == 7 ? 48 : 36);
            for (int frame = 0; frame < frames; ++frame) {
                cc::Input input;
                if (shot.which == 5) {
                    input.axisX = 1.f;
                    input.jumpEdge = frame == 12;
                    input.shootEdge = frame == 20;
                } else if (shot.which == 7) {
                    input.axisY = -1.f;
                    input.shoot = true;
                } else if (shot.which == 9) {
                    input.axisY = -1.f;
                }
                editor.updatePlay(1.f / 60.f, input);
                editor.renderPlay();
                presentPlay(renderer, texture, texW, texH, editor);
                texW = editor.playWidth();
                texH = editor.playHeight();
            }
            if (!captureFrame(renderer, path)) {
                std::cerr << "Could not save " << path.string() << '\n';
                if (texture) SDL_DestroyTexture(texture);
                return 1;
            }
            if (texture) SDL_DestroyTexture(texture);
            editor.togglePlay();
        } else {
            for (int frame = 0; frame < 4; ++frame) drawEditorFrame(window, renderer, editor, shot.which != 1);
            if (!captureFrame(renderer, path)) {
                std::cerr << "Could not save " << path.string() << '\n';
                return 1;
            }
        }
        std::cout << path.string() << '\n';
    }
    return 0;
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
        if (std::string(argv[i]) == "--selftest") {
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
                std::cerr << SDL_GetError() << '\n';
                return 1;
            }
            SDL_Window* window = SDL_CreateWindow("CabinetCore self-test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 320,
                                                  240, SDL_WINDOW_HIDDEN);
            SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : nullptr;
            cc::Editor editor;
            const int failed = editor.runSelfTest(renderer);
            const char* names[] = {"editor", "gameplay", "ui"};
            int passed[3] = {};
            int failedCount[3] = {};
            for (const auto& check : editor.selfChecks()) {
                int kind = static_cast<int>(check.kind);
                if (kind < 0 || kind > 2) kind = 0;
                if (check.pass) {
                    passed[kind]++;
                    continue;
                }
                failedCount[kind]++;
                std::cerr << "FAIL " << names[kind] << " " << check.name << '\n';
                if (!check.detail.empty()) std::cerr << "  " << check.detail << '\n';
            }
            for (int kind = 0; kind < 3; ++kind)
                std::cout << names[kind] << " selftest " << passed[kind] << " passed, " << failedCount[kind] << " failed\n";
            if (renderer) SDL_DestroyRenderer(renderer);
            if (window) SDL_DestroyWindow(window);
            SDL_Quit();
            return failed == 0 ? 0 : 1;
        }
    }

    std::string smokePath;
    std::string shotDir;
    std::string openPath;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke" && i + 1 < argc) smokePath = argv[++i];
        else if (arg == "--screenshots" && i + 1 < argc) shotDir = argv[++i];
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
    if (!shotDir.empty()) {
        const int failed = captureShowcase(window, renderer, editor, shotDir);
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return failed;
    }

    bool prevF5 = false;
    bool prevF3 = false;
    bool prevJump = false, prevShoot = false, prevStart = false, prevPause = false, prevQuit = false, prevRestart = false,
         prevCoin = false;
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
        const bool f3 = keys[SDL_SCANCODE_F3] != 0;
        if (f3 && !prevF3) editor.toggleHitboxes();
        prevF3 = f3;

        const uint64_t now = SDL_GetPerformanceCounter();
        acc += static_cast<double>(now - last) / freq;
        last = now;
        if (acc > 0.25) acc = 0.25;

        if (editor.playing()) {
            while (acc >= 1.0 / 60.0) {
                editor.updatePlay(1.f / 60.f,
                                  pollPlay(keys, pad, prevJump, prevShoot, prevStart, prevPause, prevQuit, prevRestart, prevCoin));
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
