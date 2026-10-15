#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "plugin/Plugins.hpp"

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

namespace cc {
namespace {

std::vector<LoadedPlugin> plugins_;
std::vector<int> pluginFds_;

std::string rememberedFile() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return std::string(home) + "/.config/cabinetcore/plugins";
}

std::string readLine(std::istream& in) {
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

void* openLibrary(const std::vector<uint8_t>& bytes, int& fd, std::string& err) {
    fd = memfd_create("ccplugin", MFD_CLOEXEC);
    if (fd < 0) {
        err = "Could not keep the plugin in memory.";
        return nullptr;
    }
    size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (n < 0) {
            ::close(fd);
            fd = -1;
            err = "Could not keep the plugin in memory.";
            return nullptr;
        }
        written += static_cast<size_t>(n);
    }
    if (::lseek(fd, 0, SEEK_SET) < 0) {
        ::close(fd);
        fd = -1;
        err = "Could not keep the plugin in memory.";
        return nullptr;
    }
    const std::string link = "/proc/self/fd/" + std::to_string(fd);
    void* library = dlopen(link.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        err = dlerror() ? dlerror() : "Could not open the plugin code.";
        ::close(fd);
        fd = -1;
    }
    return library;
}

bool readPackage(const std::string& path, std::string& name, std::string& author, std::string& summary, std::string& title,
                 std::vector<uint8_t>& code, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "Could not open " + path;
        return false;
    }
    if (readLine(in) != "CCPLUGIN1") {
        err = "That file is not a CabinetCore plugin.";
        return false;
    }
    name = readLine(in);
    const std::string kind = readLine(in);
    author = readLine(in);
    summary = readLine(in);
    title = readLine(in);
    const std::string sizeText = readLine(in);
    if (name.empty() || sizeText.empty()) {
        err = "That plugin file is missing its name.";
        return false;
    }
    if (kind != "block" && kind != "tool" && kind != "generator") {
        err = "That plugin file has an unknown type.";
        return false;
    }
    unsigned long size = 0;
    try {
        size = std::stoul(sizeText);
    } catch (...) {
        err = "That plugin file is damaged.";
        return false;
    }
    if (size == 0 || size > 32u * 1024u * 1024u) {
        err = "That plugin file is damaged.";
        return false;
    }
    code.resize(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(code.size()));
    if (in.gcount() != static_cast<std::streamsize>(code.size())) {
        err = "That plugin file is damaged.";
        return false;
    }
    return true;
}

}  // namespace

bool loadPlugin(const std::string& path, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    const std::string full = absolute.empty() ? path : absolute.string();
    for (const auto& plugin : plugins_) {
        if (plugin.path == full) {
            err = plugin.name + " is already loaded.";
            return false;
        }
    }
    std::string name, author, summary, title;
    std::vector<uint8_t> code;
    if (!readPackage(path, name, author, summary, title, code, err)) return false;
    int fd = -1;
    void* library = openLibrary(code, fd, err);
    if (!library) return false;
    auto* entry = reinterpret_cast<PluginDesc* (*)()>(dlsym(library, "cc_plugin_entry"));
    if (!entry) {
        err = "The plugin did not register itself.";
        dlclose(library);
        if (fd >= 0) ::close(fd);
        return false;
    }
    PluginDesc* desc = entry();
    if (!desc || desc->abi != kPluginAbi) {
        err = "This plugin was built for a different CabinetCore.";
        dlclose(library);
        if (fd >= 0) ::close(fd);
        return false;
    }
    LoadedPlugin loaded;
    loaded.name = desc->name && desc->name[0] ? desc->name : name;
    loaded.author = desc->author ? desc->author : author;
    loaded.summary = desc->summary ? desc->summary : summary;
    loaded.title = desc->title && desc->title[0] ? desc->title : (title.empty() ? loaded.name : title);
    loaded.path = full;
    loaded.kind = desc->kind;
    loaded.desc = desc;
    for (const auto& plugin : plugins_) {
        if (plugin.name == loaded.name) {
            err = "A plugin named " + loaded.name + " is already loaded.";
            dlclose(library);
            if (fd >= 0) ::close(fd);
            return false;
        }
    }
    plugins_.push_back(std::move(loaded));
    pluginFds_.push_back(fd);
    return true;
}

const std::vector<LoadedPlugin>& loadedPlugins() { return plugins_; }

const LoadedPlugin* findPlugin(const std::string& name) {
    for (const auto& plugin : plugins_)
        if (plugin.name == name) return &plugin;
    return nullptr;
}

void loadPluginsBeside(const std::string& gamePath) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(gamePath).parent_path() / "plugins";
    std::error_code error;
    if (!fs::exists(dir, error)) return;
    for (const auto& entry : fs::directory_iterator(dir, error)) {
        if (entry.path().extension() != ".ccplugin") continue;
        std::string err;
        loadPlugin(entry.path().string(), err);
    }
}

void loadRememberedPlugins() {
    const std::string file = rememberedFile();
    if (file.empty()) return;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::string err;
        loadPlugin(line, err);
    }
}

void rememberPlugin(const std::string& path) {
    const std::string file = rememberedFile();
    if (file.empty()) return;
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    const std::string full = absolute.empty() ? path : absolute.string();
    std::vector<std::string> lines;
    {
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && line != full) lines.push_back(line);
        }
    }
    lines.push_back(full);
    fs::create_directories(fs::path(file).parent_path(), error);
    std::ofstream out(file);
    for (const auto& line : lines) out << line << '\n';
}

void callPlugin(void (*fn)(PluginHost*), PluginHost& host) {
    if (!fn) return;
    try {
        fn(&host);
    } catch (const std::exception& ex) {
        if (host.log) host.log(host.user, ex.what());
    } catch (...) {
        if (host.log) host.log(host.user, "The plugin stopped on an error.");
    }
}

}  // namespace cc
