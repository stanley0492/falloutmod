#pragma once
// Per-archetype combat style parameters (roadmap P0-b4).
//
// The 112 decompiled CSTY records each hold a fixed float set; the ranges
// below were derived from those ranges (tools/combat_tuner.py re-derives them
// and writes data/combat/combat_styles.json, which the in-game plugin loads).
// Archetype assignment for the 699 decompiled factions also comes from that
// tool.

#include <string>

namespace rcai {

struct CombatStyle {
    // 0..1 ranges unless noted
    float aggression = 0.5f;      // attack weight vs defensive goals
    float caution = 0.5f;         // cover/retreat weighting
    float discipline = 0.5f;      // adherence to squad role assignment
    float coverPreference = 0.5f; // desire to hold cover between shots
    float flankBias = 0.3f;       // weight on flanking goals
    float fleeThreshold = 0.7f;   // HP fraction below which retreat/flee scores up
    float peekDuration = 1.2f;    // seconds of exposure per peek-shoot cycle
    float engageMin = 15.f;       // preferred minimum engagement distance (m)
    float engageMax = 60.f;       // preferred maximum (m)
    float reloadDiscipline = 0.5f; // tendency to break contact while reloading
};

inline CombatStyle styleForArchetype(const std::string& archetype) {
    CombatStyle s;
    if (archetype == "sniper") {
        s = {0.55f, 0.8f, 0.9f, 0.85f, 0.5f, 0.5f, 2.0f, 40.f, 120.f, 0.9f};
    } else if (archetype == "heavy") {
        s = {0.85f, 0.45f, 0.7f, 0.7f, 0.35f, 0.5f, 1.6f, 25.f, 80.f, 0.7f};
    } else if (archetype == "support") {
        s = {0.45f, 0.75f, 0.85f, 0.8f, 0.2f, 0.45f, 1.4f, 20.f, 70.f, 0.8f};
    } else if (archetype == "suicidal") {
        s = {0.95f, 0.1f, 0.15f, 0.15f, 0.6f, 0.3f, 0.8f, 3.f, 30.f, 0.2f};
    } else if (archetype == "assault") {
        s = {0.8f, 0.55f, 0.8f, 0.7f, 0.55f, 0.45f, 1.2f, 8.f, 40.f, 0.5f};
    } else { // generic
        s = {0.5f, 0.5f, 0.5f, 0.5f, 0.3f, 0.4f, 1.2f, 10.f, 50.f, 0.5f};
    }
    return s;
}

} // namespace rcai
