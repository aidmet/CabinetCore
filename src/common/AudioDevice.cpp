#include "common/AudioDevice.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstring>

namespace cc {

bool AudioDevice::open() {
    if (device_ != 0) return true;
    SDL_AudioSpec want{};
    SDL_AudioSpec have{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &AudioDevice::callback;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (device_ == 0) return false;
    freq_ = have.freq > 0 ? have.freq : 44100;
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void AudioDevice::close() {
    if (device_ == 0) return;
    SDL_CloseAudioDevice(device_);
    device_ = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.clear();
}

void AudioDevice::play(const Sound* sound, float volume, bool loop) {
    if (!sound || sound->samples.empty() || device_ == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (loop) {
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& voice) { return voice.loop; }),
                      voices_.end());
    }
    Voice voice;
    voice.data = sound->samples.data();
    voice.count = static_cast<int>(sound->samples.size());
    voice.rate = sound->sampleRate > 0 ? sound->sampleRate : 44100;
    voice.volume = volume;
    voice.loop = loop;
    voices_.push_back(voice);
}

void AudioDevice::playId(const Project& project, const std::string& id, float volume, bool loop) {
    if (id.empty()) {
        if (loop) stopLoops();
        return;
    }
    play(project.findSound(id), volume, loop);
}

void AudioDevice::stopLoops() {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& voice) { return voice.loop; }),
                  voices_.end());
}

void AudioDevice::callback(void* userdata, unsigned char* stream, int len) {
    auto* self = static_cast<AudioDevice*>(userdata);
    std::memset(stream, 0, static_cast<size_t>(len));
    const int frames = len / static_cast<int>(sizeof(int16_t) * 2);
    if (frames > 0) self->mix(reinterpret_cast<int16_t*>(stream), frames);
}

void AudioDevice::mix(int16_t* dest, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& voice : voices_) {
        if (!voice.data || voice.count <= 0) {
            voice.done = true;
            continue;
        }
        const double step = static_cast<double>(voice.rate) / static_cast<double>(std::max(1, freq_));
        for (int i = 0; i < frames; ++i) {
            if (voice.pos >= voice.count) {
                if (voice.loop) voice.pos = 0;
                else {
                    voice.done = true;
                    break;
                }
            }
            const int sample = static_cast<int>(voice.data[static_cast<int>(voice.pos)] * voice.volume);
            const int left = static_cast<int>(dest[i * 2]) + sample;
            const int right = static_cast<int>(dest[i * 2 + 1]) + sample;
            dest[i * 2] = static_cast<int16_t>(std::clamp(left, -32767, 32767));
            dest[i * 2 + 1] = static_cast<int16_t>(std::clamp(right, -32767, 32767));
            voice.pos += step;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& voice) { return voice.done; }),
                  voices_.end());
}

}  // namespace cc
