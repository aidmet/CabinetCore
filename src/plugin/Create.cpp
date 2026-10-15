#include "plugin/Plugins.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

namespace fs = std::filesystem;

struct Answers {
    std::string name;
    std::string kind;
    std::string author;
    std::string summary;
    std::string label;
    std::string out;
};

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

std::string trim(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.pop_back();
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) ++start;
    return text.substr(start);
}

bool readable(const std::string& text, size_t limit) {
    if (text.empty() || text.size() > limit) return false;
    if (text.front() == ' ' || text.back() == ' ') return false;
    for (unsigned char ch : text) {
        if (ch < 32) return false;
    }
    return true;
}

bool plainText(const std::string& text, size_t limit) {
    if (text.empty() || text.size() > limit) return false;
    for (char ch : text) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' ||
                        ch == '_';
        if (!ok) return false;
    }
    return text.front() != ' ' && text.back() != ' ';
}

std::string escape(const std::string& text) {
    std::string out;
    for (char ch : text) {
        if (ch == '\\' || ch == '"') out.push_back('\\');
        out.push_back(ch);
    }
    return out;
}

std::string ask(const std::string& prompt, const std::string& fallback) {
    if (!fallback.empty()) return fallback;
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) return {};
    return trim(line);
}

bool copyFile(const fs::path& from, const fs::path& to, std::string& err) {
    std::ifstream in(from, std::ios::binary);
    if (!in) {
        err = "Could not read " + from.string();
        return false;
    }
    std::ofstream out(to, std::ios::binary);
    if (!out) {
        err = "Could not write " + to.string();
        return false;
    }
    out << in.rdbuf();
    if (!out) {
        err = "Could not write " + to.string();
        return false;
    }
    return true;
}

std::string starterBody(const std::string& kind) {
    if (kind == "tool") {
        return
            "static void onTool(cc::PluginHost* host) {\n"
            "    if (!host || !host->paintTile) return;\n"
            "    host->paintTile(host->user, host->tileX, host->tileY, 1);\n"
            "    if (host->setStatus) host->setStatus(host->user, \"Painted a tile.\");\n"
            "}\n";
    }
    if (kind == "generator") {
        return
            "static void onGenerate(cc::PluginHost* host) {\n"
            "    if (!host || !host->paintTile) return;\n"
            "    const int ground = host->stageH > 0 ? host->stageH - 1 : 0;\n"
            "    for (int x = 0; x < host->stageW; ++x) host->paintTile(host->user, x, ground, 1);\n"
            "    if (host->placeActor && host->tileW > 0 && host->tileH > 0) {\n"
            "        const float x = static_cast<float>(2 * host->tileW);\n"
            "        const float y = static_cast<float>((host->stageH > 1 ? host->stageH - 2 : 0) * host->tileH);\n"
            "        host->placeActor(host->user, \"runner\", x, y);\n"
            "    }\n"
            "    if (host->setStatus) host->setStatus(host->user, \"Generated a floor.\");\n"
            "}\n";
    }
    return
        "static void onBlock(cc::PluginHost* host) {\n"
        "    if (!host || !host->addScore) return;\n"
        "    host->addScore(host->user, 25);\n"
        "    if (host->log) host->log(host->user, \"Scored from the plugin.\");\n"
        "}\n";
}

const char* kindEnum(const std::string& kind) {
    if (kind == "tool") return "Tool";
    if (kind == "generator") return "Generator";
    return "Block";
}

const char* callbackName(const std::string& kind) {
    if (kind == "tool") return "onTool";
    if (kind == "generator") return "onGenerate";
    return "onBlock";
}

const char* callbackField(const std::string& kind) {
    if (kind == "tool") return "onTool";
    if (kind == "generator") return "onGenerate";
    return "onBlock";
}

bool writePlugin(const fs::path& root, const Answers& answers, std::string& err) {
    std::ofstream source(root / "src" / "plugin.cpp");
    if (!source) {
        err = "Could not write src/plugin.cpp.";
        return false;
    }
    const std::string field = callbackField(answers.kind);
    source << "// CabinetCore plugin. Edit this file, then run ./build in this folder.\n"
           << "// sdk/include/cc has the CabinetCore types and the plugin host.\n"
           << "// paintTile uses tile coordinates. placeActor uses pixel coordinates.\n"
           << "#include \"cc/Plugin.hpp\"\n"
           << "#include \"cc/Types.hpp\"\n\n"
           << starterBody(answers.kind) << "\n"
           << "extern \"C\" cc::PluginDesc* cc_plugin_entry() {\n"
           << "    static cc::PluginDesc desc;\n"
           << "    desc.abi = cc::kPluginAbi;\n"
           << "    desc.kind = cc::PluginKind::" << kindEnum(answers.kind) << ";\n"
           << "    desc.name = \"" << escape(answers.name) << "\";\n"
           << "    desc.author = \"" << escape(answers.author) << "\";\n"
           << "    desc.summary = \"" << escape(answers.summary) << "\";\n"
           << "    desc.title = \"" << escape(answers.label) << "\";\n"
           << "    desc." << field << " = " << callbackName(answers.kind) << ";\n"
           << "    return &desc;\n"
           << "}\n";
    if (!source) {
        err = "Could not write src/plugin.cpp.";
        return false;
    }
    std::ofstream meta(root / "plugin.txt");
    if (!meta) {
        err = "Could not write plugin.txt.";
        return false;
    }
    meta << answers.name << '\n' << answers.kind << '\n' << answers.author << '\n' << answers.summary << '\n' << answers.label << '\n';
    return static_cast<bool>(meta);
}

bool createPlugin(const Answers& answers, std::string& err) {
    const char* rootEnv = std::getenv("CABINET_ROOT");
#ifndef CABINET_ROOT
#define CABINET_ROOT ""
#endif
    const fs::path cabinet = rootEnv && *rootEnv ? fs::path(rootEnv) : fs::path(CABINET_ROOT);
    if (cabinet.empty() || !fs::exists(cabinet / "src" / "common" / "Types.hpp")) {
        err = "Could not find the CabinetCore headers.";
        return false;
    }
    const fs::path folder = answers.out.empty() ? fs::path(answers.name) : fs::path(answers.out);
    if (fs::exists(folder)) {
        err = folder.string() + " already exists.";
        return false;
    }
    std::error_code error;
    fs::create_directories(folder / "sdk" / "include" / "cc", error);
    fs::create_directories(folder / "src", error);
    if (error) {
        err = "Could not create " + folder.string();
        return false;
    }
    if (!copyFile(cabinet / "src" / "common" / "Types.hpp", folder / "sdk" / "include" / "cc" / "Types.hpp", err)) return false;
    if (!copyFile(cabinet / "src" / "plugin" / "Plugin.hpp", folder / "sdk" / "include" / "cc" / "Plugin.hpp", err)) return false;
    if (!copyFile(cabinet / "src" / "plugin" / "build_tool.cpp", folder / "build.cpp", err)) return false;
    if (!writePlugin(folder, answers, err)) return false;
    const std::string compile = "g++ -std=c++17 -O2 -o \"" + (folder / "build").string() + "\" \"" + (folder / "build.cpp").string() + "\"";
    if (std::system(compile.c_str()) != 0) {
        err = "Wrote the plugin folder, but could not compile ./build. From that folder, run: g++ -std=c++17 -O2 -o build build.cpp";
        return false;
    }
    ::chmod((folder / "build").string().c_str(), 0755);
    std::cout << "Wrote " << folder.string() << "\n"
              << "The sdk has the CabinetCore types. src/plugin.cpp registers the plugin.\n"
              << "Edit it, then run ./build inside the folder. That writes " << answers.name << ".ccplugin.\n"
              << "In the editor, choose File, Load plugin.\n";
    return true;
}

void usage() {
    std::cout << "Usage: cabinet-plugin\n"
              << "       cabinet-plugin create --name Name --type block|tool|generator [--author Name] [--summary Text] [--label Text] [--out Folder]\n"
              << "With no arguments, it asks for the plugin name, type, author, summary, and label.\n";
}

bool takeFlag(int& i, int argc, char** argv, const std::string& flag, std::string& value) {
    if (std::string(argv[i]) != flag) return false;
    if (i + 1 >= argc) return false;
    value = argv[++i];
    return true;
}

int selfTest() {
    const fs::path dir = fs::temp_directory_path() / "cabinetcore-plugin-test";
    std::error_code error;
    fs::remove_all(dir, error);
    Answers answers;
    answers.name = "Blink Test";
    answers.kind = "block";
    answers.author = "CabinetCore";
    answers.summary = "Adds a little score";
    answers.label = "blink";
    answers.out = dir.string();
    std::string err;
    if (!createPlugin(answers, err)) return fail(err);
    const std::string build = (dir / "build").string();
    if (std::system(("\"" + build + "\"").c_str()) != 0) return fail("The plugin build failed.");
    const fs::path packed = dir / "Blink Test.ccplugin";
    if (!fs::exists(packed)) return fail("The build did not write a plugin file.");
    if (!cc::loadPlugin(packed.string(), err)) return fail(err);
    const cc::LoadedPlugin* plugin = cc::findPlugin("Blink Test");
    if (!plugin || !plugin->desc || !plugin->desc->onBlock) return fail("The plugin did not register a block.");
    int score = 0;
    cc::PluginHost host;
    host.user = &score;
    host.addScore = [](void* user, int amount) { *static_cast<int*>(user) += amount; };
    cc::callPlugin(plugin->desc->onBlock, host);
    if (score != 25) return fail("The sample block did not add 25 to the score.");
    fs::remove_all(dir, error);
    std::cout << "plugin selftest passed\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--selftest") return selfTest();

    Answers answers;
    bool create = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "create") {
            create = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (takeFlag(i, argc, argv, "--name", answers.name) || takeFlag(i, argc, argv, "--type", answers.kind) ||
            takeFlag(i, argc, argv, "--author", answers.author) || takeFlag(i, argc, argv, "--summary", answers.summary) ||
            takeFlag(i, argc, argv, "--label", answers.label) || takeFlag(i, argc, argv, "--out", answers.out))
            continue;
        return fail("Unknown argument " + arg + ". Run cabinet-plugin --help.");
    }

    const bool interactive = argc == 1 || (create && answers.name.empty());
    if (!interactive && answers.name.empty() && !isatty(0)) {
        usage();
        return 1;
    }
    if (answers.name.empty()) {
        std::cout << "CabinetCore plugin maker\n";
        answers.name = ask("Plugin name: ", {});
    }
    answers.name = trim(answers.name);
    if (!plainText(answers.name, 48))
        return fail("Use a name of letters, numbers, spaces, hyphens, or underscores.");

    if (answers.kind.empty()) {
        answers.kind = ask("Type (block, tool, or generator): ", {});
        answers.kind = trim(answers.kind);
    }
    for (char& ch : answers.kind)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    if (answers.kind != "block" && answers.kind != "tool" && answers.kind != "generator")
        return fail("Type must be block, tool, or generator.");

    if (answers.author.empty() && (interactive || isatty(0))) answers.author = ask("Author: ", {});
    answers.author = trim(answers.author);
    if (answers.author.empty()) answers.author = "Anonymous";
    if (!readable(answers.author, 64)) return fail("The author is too long or has a line break.");

    if (answers.summary.empty() && (interactive || isatty(0))) answers.summary = ask("Short summary: ", {});
    answers.summary = trim(answers.summary);
    if (answers.summary.empty()) answers.summary = "A CabinetCore plugin";
    if (!readable(answers.summary, 160)) return fail("The summary is too long or has a line break.");

    if (answers.label.empty() && (interactive || isatty(0))) answers.label = ask("Label shown in the editor: ", {});
    answers.label = trim(answers.label);
    if (answers.label.empty()) answers.label = answers.name;
    if (!plainText(answers.label, 48))
        return fail("The label can use letters, numbers, spaces, hyphens, or underscores.");

    std::string err;
    if (!createPlugin(answers, err)) return fail(err);
    return 0;
}
