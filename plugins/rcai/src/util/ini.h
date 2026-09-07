#pragma once
// Tiny INI reader/writer (key = value, [sections], ; comments).
// Platform-independent so the auto-tuner can be unit-tested off-Windows;
// on Windows the same file format is what the game INIs use.
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace rcai {

class IniFile {
public:
    using Section = std::vector<std::pair<std::string, std::string>>;

    static IniFile load(const std::string& path) {
        IniFile ini;
        std::ifstream in(path);
        if (!in) throw std::runtime_error("ini: cannot open " + path);
        std::string line, section = "Default";
        while (std::getline(in, line)) {
            const std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;
            if (trimmed.front() == '[' && trimmed.back() == ']') {
                section = trimmed.substr(1, trimmed.size() - 2);
                continue;
            }
            const size_t eq = trimmed.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(trimmed.substr(0, eq));
            std::string val = trim(trimmed.substr(eq + 1));
            const size_t comment = val.find_first_of(";#");
            if (comment != std::string::npos) val = trim(val.substr(0, comment));
            ini.set(section, key, val);
        }
        return ini;
    }

    void save(const std::string& path) const {
        std::ofstream out(path);
        if (!out) throw std::runtime_error("ini: cannot write " + path);
        bool firstSection = true;
        for (const auto& [section, kv] : sections_) {
            if (!firstSection) out << "\n";
            firstSection = false;
            out << "[" << section << "]\n";
            for (const auto& [k, v] : kv) out << k << " = " << v << "\n";
        }
    }

    bool has(const std::string& section, const std::string& key) const {
        const auto* s = findSection(section);
        if (!s) return false;
        for (const auto& kv : *s) if (kv.first == key) return true;
        return false;
    }

    std::string get(const std::string& section, const std::string& key,
                    const std::string& def = "") const {
        const auto* s = findSection(section);
        if (!s) return def;
        for (const auto& kv : *s)
            if (kv.first == key) return kv.second;
        return def;
    }

    int getInt(const std::string& section, const std::string& key, int def = 0) const {
        const std::string v = get(section, key, "");
        return v.empty() ? def : std::stoi(v);
    }

    float getFloat(const std::string& section, const std::string& key, float def = 0.f) const {
        const std::string v = get(section, key, "");
        return v.empty() ? def : std::stof(v);
    }

    void set(const std::string& section, const std::string& key, const std::string& value) {
        Section* s = nullptr;
        for (auto& sec : sections_)
            if (sec.first == section) { s = &sec.second; break; }
        if (!s) {
            sections_.emplace_back(section, Section{});
            s = &sections_.back().second;
        }
        for (auto& kv : *s)
            if (kv.first == key) { kv.second = value; return; }
        s->emplace_back(key, value);
    }

    const std::vector<std::pair<std::string, Section>>& sections() const { return sections_; }

private:
    std::vector<std::pair<std::string, Section>> sections_;

    static std::string trim(const std::string& s) {
        const size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        const size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    const Section* findSection(const std::string& name) const {
        for (const auto& sec : sections_)
            if (sec.first == name) return &sec.second;
        return nullptr;
    }
};

} // namespace rcai
