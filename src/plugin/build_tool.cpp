#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string exeDir() {
    char buffer[4096];
    const ssize_t count = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (count <= 0) return fs::current_path().string();
    buffer[count] = 0;
    return fs::path(buffer).parent_path().string();
}

std::string readLine(std::istream& in) {
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

}  // namespace

int main() {
    const fs::path root(exeDir());
    std::ifstream meta(root / "plugin.txt");
    if (!meta) return fail("Could not read plugin.txt. Run this from the plugin folder.");
    const std::string name = readLine(meta);
    const std::string kind = readLine(meta);
    const std::string author = readLine(meta);
    const std::string summary = readLine(meta);
    const std::string title = readLine(meta);
    if (name.empty() || kind.empty()) return fail("plugin.txt is missing the plugin name.");
    if (!fs::exists(root / "src" / "plugin.cpp")) return fail("Could not find src/plugin.cpp.");
    if (!fs::exists(root / "sdk" / "include" / "cc" / "Plugin.hpp")) return fail("Could not find the plugin sdk.");

    const fs::path object = root / "plugin.so";
    const std::string compile = "g++ -std=c++17 -fPIC -shared -I \"" + (root / "sdk" / "include").string() + "\" \"" +
                                (root / "src" / "plugin.cpp").string() + "\" -o \"" + object.string() + "\"";
    if (std::system(compile.c_str()) != 0) return fail("The plugin did not compile.");

    std::ifstream source(object, std::ios::binary);
    std::vector<char> code((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    std::error_code error;
    fs::remove(object, error);
    if (code.empty()) return fail("The compiler did not write plugin code.");

    const fs::path out = root / (name + ".ccplugin");
    std::ofstream packed(out, std::ios::binary);
    if (!packed) return fail("Could not write " + out.string());
    packed << "CCPLUGIN1\n" << name << '\n' << kind << '\n' << author << '\n' << summary << '\n' << title << '\n' << code.size()
           << '\n';
    packed.write(code.data(), static_cast<std::streamsize>(code.size()));
    if (!packed) return fail("Could not finish " + out.string());
    std::cout << "Wrote " << out.string() << "\nLoad it from the editor with File, Load plugin.\n";
    return 0;
}
