#pragma once

#include "common/Types.hpp"

namespace cc {

Sound makeSilence(const std::string& id, const std::string& name, float seconds, bool music);
Sound synthJump();
Sound synthShoot();
Sound synthCoin();
Sound synthHurt();
Sound synthExplode();
Sound synthGoal();
Sound synthPickup();
Sound synthMusic(const std::string& id, const std::string& name, const int* notes, int count, float bpm);

void addStandardSounds(Project& project);

}  // namespace cc
