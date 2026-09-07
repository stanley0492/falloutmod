#pragma once
// P0-b5 · Anti-cheese & adaptive difficulty.
//
//  Camp detection: the player habit grid (threat.h) reports a concentrated,
//  long-duration position while the actor has observed shots -> the squad
//  stops shooting straight at the position: flanker weight doubles, and the
//  leader periodically emits a "probe" alert at an offset position to bait
//  the player into revealing movement.
//
//  Adaptive difficulty: tracks player DPS and accuracy over a rolling
//  window; scales the combat-style aggression within +/-25% and the flee
//  threshold within a safe band (the same band the decompiled fCombat GMSTs
//  span). Transparent via debug output.

#include <cstdio>

#include "ai/threat.h"
#include "world/world.h"

namespace rcai {

struct AntiCheeseState {
    bool campingDetected = false;
    float campDetectedAt = -999.f;
    float lastProbeTime = -999.f;
    bool probeRequested = false; // transient, cleared each tick

    // Rolling player combat stats.
    float windowStart = -1.f;
    float playerDamageDealt = 0.f;
    int shotsFired = 0;
    int shotsHit = 0;

    // Resulting scalars (read by the behaviour layer).
    float aggressionScale = 1.f;   // 0.75..1.25
    float fleeShift = 0.f;         // -0.1..+0.1 added to fleeThreshold
    float flankBoost = 1.f;        // 1.0 or 2.0

    std::string describe() const {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "anti-cheese: camp=%d flankBoost=%.1f aggressionScale=%.2f fleeShift=%.2f",
                      campingDetected ? 1 : 0, flankBoost, aggressionScale, fleeShift);
        return buf;
    }
};

class AntiCheese {
public:
    void onPlayerShot(const WorldSnapshot& w, bool hit) {
        if (st_.windowStart < 0.f) st_.windowStart = w.time;
        ++st_.shotsFired;
        if (hit) ++st_.shotsHit;
    }

    void onPlayerDamage(const WorldSnapshot& w, float dmg) {
        if (st_.windowStart < 0.f) st_.windowStart = w.time;
        st_.playerDamageDealt += dmg;
    }

    // Returns the probe target (valid only when probeRequested).
    Vec2 tick(const WorldSnapshot& w, const PlayerHabitGrid& habits, bool combatEngaged) {
        st_.probeRequested = false;

        const float window = std::max(1.f, w.time - st_.windowStart);
        const float dps = st_.playerDamageDealt / window;
        const float accuracy = st_.shotsFired > 0 ? float(st_.shotsHit) / st_.shotsFired : 0.5f;

        // Reset the window every 60 s so stale stats don't dominate.
        if (window > 60.f) {
            st_.windowStart = w.time;
            st_.playerDamageDealt = 0.f;
            st_.shotsFired = 0;
            st_.shotsHit = 0;
        }

        // --- adaptive difficulty -----------------------------------------
        // Baseline: dps 4 and accuracy 0.5 are "average".
        const float difficulty = clamp(0.5f * (dps / 4.f) + 0.5f * (accuracy / 0.5f), 0.f, 1.f);
        st_.aggressionScale = 0.75f + 0.5f * difficulty; // 0.75 .. 1.25
        st_.fleeShift = 0.2f * (0.5f - difficulty);      // tough players: AI flees sooner

        // --- camp detection ------------------------------------------------
        const bool campNow =
            combatEngaged && habits.concentrated(/*minSeconds*/ 30.f, /*share*/ 0.6f) &&
            st_.shotsFired >= 3;
        if (campNow && !st_.campingDetected) {
            st_.campingDetected = true;
            st_.campDetectedAt = w.time;
        }
        if (st_.campingDetected && !campNow && (w.time - st_.campDetectedAt > 15.f))
            st_.campingDetected = false; // player moved on

        st_.flankBoost = st_.campingDetected ? 2.f : 1.f;

        // Probe every 20 s while camping is active.
        if (st_.campingDetected && (w.time - st_.lastProbeTime > 20.f)) {
            st_.lastProbeTime = w.time;
            st_.probeRequested = true;
            const float a = w.time * 0.7f; // deterministic-ish wandering angle
            const Vec2 off{std::cos(a) * 35.f, std::sin(a) * 35.f};
            return w.player.pos + off;
        }
        return {0, 0};
    }

    const AntiCheeseState& state() const { return st_; }

private:
    AntiCheeseState st_;
};

} // namespace rcai
