#include "platform/settings.h"
#include <fstream>

namespace dm {

namespace {
std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
} // namespace

bool Settings::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        values_[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return true;
}

bool Settings::save(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "# Lightmere settings (rewritten by the program)\n";
    for (const auto& [k, v] : values_) out << k << '=' << v << '\n';
    return bool(out);
}

std::string Settings::get(const std::string& key, const std::string& def) const {
    auto it = values_.find(key);
    return it != values_.end() ? it->second : def;
}

} // namespace dm
