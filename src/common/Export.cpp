#include "common/Export.hpp"

#include <filesystem>

namespace cc {

bool exportGame(const Project& project, const std::string& outDir, const std::string& playerPath, std::string& err) {
    const std::string problem = project.validate();
    if (!problem.empty()) {
        err = problem;
        return false;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        err = "Could not create " + outDir;
        return false;
    }
    const fs::path dat = fs::path(outDir) / "game.dat";
    if (!project.save(dat.string(), err)) return false;

    const fs::path src(playerPath);
    if (playerPath.empty() || !fs::exists(src)) {
        err = "Could not find the cabinet player at " + playerPath;
        return false;
    }
    const fs::path dest = fs::path(outDir) / "cabinet.elf";
    if (fs::absolute(src) != fs::absolute(dest)) {
        fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            err = "Could not copy cabinet.elf: " + ec.message();
            return false;
        }
    }
    fs::permissions(dest,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    return true;
}

}  // namespace cc
