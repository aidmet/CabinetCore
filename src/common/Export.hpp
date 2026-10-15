#pragma once

#include "common/Types.hpp"

#include <string>

namespace cc {

bool exportGame(const Project& project, const std::string& outDir, const std::string& playerPath, std::string& err);

}  // namespace cc
