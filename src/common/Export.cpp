#include "common/Export.hpp"

#include "plugin/Plugins.hpp"

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
    if (!loadedPlugins().empty()) {
        const fs::path plugins = fs::path(outDir) / "plugins";
        fs::create_directories(plugins, ec);
        if (ec) {
            err = "Could not create the plugins folder.";
            return false;
        }
        for (const auto& plugin : loadedPlugins()) {
            const fs::path from(plugin.path);
            if (!fs::exists(from)) continue;
            fs::copy_file(from, plugins / from.filename(), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                err = "Could not copy " + from.filename().string();
                return false;
            }
        }
    }
    return true;
}

}  // namespace cc
