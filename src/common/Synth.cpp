#include "common/Synth.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cc {
namespace {

constexpr int kRate = 44100;

float midiFreq(int note) {
    return 440.f * std::pow(2.f, (note - 69) / 12.f);
}

void mixNote(std::vector<int16_t>& dest, double start, double dur, double freq, double volume, int wave) {
    const int from = std::max(0, static_cast<int>(start * kRate));
    const int len = std::max(1, static_cast<int>(dur * kRate));
    if (from + len > static_cast<int>(dest.size())) dest.resize(from + len, 0);
    for (int i = 0; i < len; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double env = std::min(1.0, i / 200.0) * std::min(1.0, (len - i) / 400.0);
        double s = 0.0;
        const double phase = t * freq;
        if (wave == 0) {
            s = std::sin(phase * 6.28318530718);
        } else if (wave == 1) {
            s = (phase - std::floor(phase)) < 0.5 ? 1.0 : -1.0;
        } else if (wave == 2) {
            s = 2.0 * (phase - std::floor(phase)) - 1.0;
        } else {
            s = (static_cast<int>(phase * 8) & 1) ? 0.6 : -0.6;
            s += 0.4 * std::sin(phase * 6.28318530718);
        }
        const int sample = static_cast<int>(dest[from + i] + s * env * volume * 32767.0);
        dest[from + i] = static_cast<int16_t>(std::clamp(sample, -32767, 32767));
    }
}

Sound finish(const std::string& id, const std::string& name, std::vector<int16_t> samples, bool music) {
    Sound sound;
    sound.id = id;
    sound.name = name;
    sound.sampleRate = kRate;
    sound.channels = 1;
    sound.music = music;
    sound.samples = std::move(samples);
    return sound;
}

}  // namespace

Sound makeSilence(const std::string& id, const std::string& name, float seconds, bool music) {
    return finish(id, name, std::vector<int16_t>(static_cast<size_t>(kRate * seconds), 0), music);
}

Sound synthJump() {
    std::vector<int16_t> s;
    for (int i = 0; i < 12; ++i) mixNote(s, i * 0.008, 0.02, 700 - i * 35, 0.35, 1);
    return finish("jump", "Jump", std::move(s), false);
}

Sound synthShoot() {
    std::vector<int16_t> s;
    mixNote(s, 0, 0.07, 880, 0.28, 1);
    mixNote(s, 0, 0.05, 220, 0.2, 3);
    return finish("shoot", "Shoot", std::move(s), false);
}

Sound synthCoin() {
    std::vector<int16_t> s;
    mixNote(s, 0.0, 0.08, 988, 0.35, 1);
    mixNote(s, 0.07, 0.12, 1319, 0.35, 1);
    return finish("coin", "Coin", std::move(s), false);
}

Sound synthHurt() {
    std::vector<int16_t> s;
    mixNote(s, 0, 0.18, 140, 0.45, 1);
    mixNote(s, 0, 0.18, 90, 0.3, 3);
    return finish("hurt", "Hurt", std::move(s), false);
}

Sound synthExplode() {
    std::vector<int16_t> s(static_cast<size_t>(kRate * 0.28), 0);
    uint32_t n = 22222;
    for (size_t i = 0; i < s.size(); ++i) {
        n = n * 1664525u + 1013904223u;
        const double env = 1.0 - static_cast<double>(i) / s.size();
        const double noise = ((n >> 16) & 0xFFFF) / 65535.0 * 2.0 - 1.0;
        s[i] = static_cast<int16_t>(std::clamp(noise * 12000.0 * env, -32767.0, 32767.0));
    }
    return finish("explode", "Explode", std::move(s), false);
}

Sound synthGoal() {
    std::vector<int16_t> s;
    const int notes[] = {72, 76, 79, 84};
    for (int i = 0; i < 4; ++i) mixNote(s, i * 0.09, 0.16, midiFreq(notes[i]), 0.35, 1);
    return finish("goal", "Goal", std::move(s), false);
}

Sound synthPickup() {
    std::vector<int16_t> s;
    mixNote(s, 0, 0.1, 660, 0.3, 0);
    mixNote(s, 0.08, 0.14, 880, 0.3, 0);
    return finish("pickup", "Pickup", std::move(s), false);
}

Sound synthMusic(const std::string& id, const std::string& name, const int* notes, int count, float bpm) {
    std::vector<int16_t> s;
    const double step = 60.0 / bpm / 2.0;
    double t = 0.0;
    for (int i = 0; i + 1 < count; i += 2) {
        const int note = notes[i];
        const int beats = std::max(1, notes[i + 1]);
        if (note > 0) {
            mixNote(s, t, beats * step * 0.9, midiFreq(note), 0.22, 1);
            mixNote(s, t, beats * step * 0.9, midiFreq(note - 12), 0.12, 2);
        }
        t += beats * step;
    }
    if (s.empty()) s.resize(kRate / 2, 0);
    return finish(id, name, std::move(s), true);
}

Sound synthMade(const std::string& id, const std::string& name, int style, int note, float seconds, bool music) {
    std::vector<int16_t> samples;
    const int midi = std::clamp(note, 24, 108);
    const double freq = midiFreq(midi);
    const double dur = std::clamp(static_cast<double>(seconds), 0.05, 4.0);
    if (style == 4) {
        samples.assign(static_cast<size_t>(kRate * dur), 0);
        uint32_t n = 1664525u + static_cast<uint32_t>(midi * 97);
        for (size_t i = 0; i < samples.size(); ++i) {
            n = n * 1664525u + 1013904223u;
            const double env = 1.0 - static_cast<double>(i) / samples.size();
            const double noise = ((n >> 16) & 0xFFFF) / 65535.0 * 2.0 - 1.0;
            samples[i] = static_cast<int16_t>(std::clamp(noise * 14000.0 * env, -32767.0, 32767.0));
        }
    } else if (style == 2 || style == 3) {
        const int steps = 8;
        for (int i = 0; i < steps; ++i) {
            const int shifted = style == 2 ? midi + i * 2 : midi - i * 2;
            mixNote(samples, i * dur / steps, dur / steps * 1.15, midiFreq(std::clamp(shifted, 24, 108)), 0.34, 1);
        }
    } else if (style == 5) {
        mixNote(samples, 0, dur * 0.45, freq, 0.34, 1);
        mixNote(samples, dur * 0.4, dur * 0.6, midiFreq(std::min(108, midi + 7)), 0.34, 1);
    } else if (style == 6) {
        const int notes[] = {midi, midi + 4, midi + 7};
        for (int i = 0; i < 3; ++i) mixNote(samples, i * dur / 3.0, dur / 2.2, midiFreq(std::min(108, notes[i])), 0.32, 1);
    } else {
        mixNote(samples, 0, dur, freq, 0.36, style == 1 ? 0 : 1);
    }
    if (samples.empty()) samples.resize(kRate / 8, 0);
    return finish(id, name, std::move(samples), music);
}

void addStandardSounds(Project& project) {
    project.sounds.push_back(synthJump());
    project.sounds.push_back(synthShoot());
    project.sounds.push_back(synthCoin());
    project.sounds.push_back(synthHurt());
    project.sounds.push_back(synthExplode());
    project.sounds.push_back(synthGoal());
    project.sounds.push_back(synthPickup());

    const int run[] = {60, 1, 0, 1, 64, 1, 67, 1, 64, 1, 60, 2, 67, 1, 72, 2,
                       55, 1, 0, 1, 59, 1, 62, 1, 59, 1, 55, 2, 62, 1, 67, 2};
    const int shoot[] = {48, 1, 55, 1, 60, 1, 55, 1, 48, 1, 58, 1, 63, 1, 58, 1,
                         46, 1, 53, 1, 58, 1, 53, 1, 46, 1, 56, 1, 61, 1, 68, 2};
    const int maze[] = {63, 2, 0, 1, 62, 1, 60, 2, 0, 1, 58, 2, 63, 2, 67, 2, 65, 2, 63, 4};
    const int boss[] = {40, 1, 40, 1, 47, 1, 40, 1, 43, 1, 40, 1, 48, 2, 36, 1, 36, 1, 43, 1, 48, 2};
    project.sounds.push_back(synthMusic("music_run", "Rooftop theme", run, 32, 150));
    project.sounds.push_back(synthMusic("music_shoot", "Raid theme", shoot, 32, 170));
    project.sounds.push_back(synthMusic("music_maze", "Maze theme", maze, 20, 120));
    project.sounds.push_back(synthMusic("music_boss", "Boss theme", boss, 22, 140));
}

}  // namespace cc
