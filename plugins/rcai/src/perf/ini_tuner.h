#pragma once
// P0-a2/a4 · INI auto-tuner.
//
// Rewrites the game's INIs toward a modern, stable baseline derived from the
// decompiled configuration map (engine_configuration_subsystems.json):
//
//   [Papyrus]  fUpdateBudgetMS raised (headroom for the scheduler),
//              fExtraTaskletBudgetMS for background tasklets
//   [HAVOK]    fMaxTime clamped for high-FPS physics stability
//   [General]  uGridsToLoad / cell buffers sized for the streaming manager
//   [Display]  TAA off when the modern upscaler handles AA
//
// Idempotent: re-running never duplicates keys; every change is logged to the
// returned report (also written as JSON by the caller for crash-watchdog
// correlation).

#include <string>
#include <vector>

#include "../util/ini.h"

namespace rcai::perf {

struct TunerReport {
    std::vector<std::string> changes; // "section.key: old -> new"
};

struct TunerSettings {
    float papyrusUpdateBudgetMs = 2.0f;
    float papyrusExtraTaskletMs = 1.0f;
    float havokMaxTime = 0.016f; // clamp ~60 Hz physics regardless of display FPS
    int gridsToLoad = 5;
    int interiorCellBuffer = 8;
    int exteriorCellBuffer = 12;
    bool taaEnabled = false;
};

class IniTuner {
public:
    static TunerReport tune(const std::string& iniPath, const TunerSettings& s) {
        TunerReport rep;
        IniFile ini;
        try {
            ini = IniFile::load(iniPath);
        } catch (...) {
            // Missing file: create from settings.
        }
        apply(&ini, "Papyrus", "fUpdateBudgetMS", fmt(s.papyrusUpdateBudgetMs), rep);
        apply(&ini, "Papyrus", "fExtraTaskletBudgetMS", fmt(s.papyrusExtraTaskletMs), rep);
        apply(&ini, "HAVOK", "fMaxTime", fmt(s.havokMaxTime), rep);
        apply(&ini, "General", "uGridsToLoad", std::to_string(s.gridsToLoad), rep);
        apply(&ini, "General", "uInterior Cell Buffer", std::to_string(s.interiorCellBuffer), rep);
        apply(&ini, "General", "uExterior Cell Buffer", std::to_string(s.exteriorCellBuffer), rep);
        apply(&ini, "Display", "bUseTAA", s.taaEnabled ? "1" : "0", rep);
        ini.save(iniPath);
        return rep;
    }

private:
    static std::string fmt(float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4f", v);
        return buf;
    }

    static void apply(IniFile* ini, const std::string& section, const std::string& key,
                      const std::string& value, TunerReport& rep) {
        const std::string old = ini->get(section, key, "");
        if (old != value) {
            rep.changes.push_back(section + "." + key + ": " + (old.empty() ? "(unset)" : old) +
                                  " -> " + value);
            ini->set(section, key, value);
        }
    }
};

} // namespace rcai::perf
