#pragma once

#include "plugin/Plugin.hpp"

#include <string>
#include <vector>

namespace cc {

struct LoadedPlugin {
    std::string name;
    std::string author;
    std::string summary;
    std::string title;
    std::string path;
    PluginKind kind = PluginKind::Block;
    PluginDesc* desc = nullptr;
};

bool loadPlugin(const std::string& path, std::string& err);
const std::vector<LoadedPlugin>& loadedPlugins();
const LoadedPlugin* findPlugin(const std::string& name);
void loadPluginsBeside(const std::string& gamePath);
void loadRememberedPlugins();
void rememberPlugin(const std::string& path);
void callPlugin(void (*fn)(PluginHost*), PluginHost& host);

}  // namespace cc
