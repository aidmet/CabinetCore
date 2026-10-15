#pragma once

#include "common/Types.hpp"

#include <mutex>
#include <string>
#include <vector>

struct SDL_AudioSpec;

namespace cc {

class AudioDevice {
public:
    bool open();
    void close();
    void play(const Sound* sound, float volume, bool loop);
    void playId(const Project& project, const std::string& id, float volume = 1.f, bool loop = false);
    void stopLoops();

private:
    struct Voice {
        const int16_t* data = nullptr;
        int count = 0;
        int rate = 44100;
        double pos = 0;
        float volume = 1.f;
        bool loop = false;
        bool done = false;
    };

    static void callback(void* userdata, unsigned char* stream, int len);
    void mix(int16_t* dest, int frames);

    std::mutex mutex_;
    std::vector<Voice> voices_;
    uint32_t device_ = 0;
    int freq_ = 44100;
};

}  // namespace cc
