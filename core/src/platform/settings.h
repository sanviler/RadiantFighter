#pragma once
// Settings kept between runs: a text file with "key=value" lines
// (lightmere.ini next to the executable). Unknown or malformed lines are ignored.
#include <map>
#include <string>

namespace dm {

class Settings {
public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    std::string get(const std::string& key, const std::string& def = {}) const;
    void set(const std::string& key, const std::string& value) { values_[key] = value; }

private:
    std::map<std::string, std::string> values_;
};

} // namespace dm
