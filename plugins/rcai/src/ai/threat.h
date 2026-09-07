#pragma once
// P0-b1 (part 2) · Threat model + memory.
//
// threat = continuous estimate of how dangerous the *player* is to this actor,
// updated from observed damage and accuracy. Drives flee/retreat scoring and
// the adaptive-difficulty governor (anti_cheese.h).
//
// Memory: last-seen positions (with age) plus a coarse position histogram of
// player behaviour — the histogram feeds camp detection.

#include "world/world.h"

#include <array>

namespace rcai {

inline void updateThreat(const WorldSnapshot& w, ActorState& actor, float dt,
                         /* observed player shot */ bool playerShotThisActor,
                         /* player hit */ bool playerHit) {
    (void)w;
    if (playerShotThisActor) {
        const float gain = playerHit ? 0.18f : 0.05f;
        actor.threat = clamp(actor.threat + gain, 0.f, 1.f);
    }
    // Slow forgetting.
    actor.threat = clamp(actor.threat - 0.002f * dt, 0.f, 1.f);
}

// Age of the actor's last player sighting (seconds).
inline float lastSeenAge(const WorldSnapshot& w, const ActorState& a) {
    if (a.lastSeenTime < -100.f) return 1e9f;
    return std::max(0.f, w.time - a.lastSeenTime);
}

// Player habit histogram: 4x4 grid over a 120 m box centred on the cell.
// Camp detection (anti_cheese.h) reads the occupancy concentration.
class PlayerHabitGrid {
public:
    static constexpr int kCells = 4;
    static constexpr float kExtent = 120.f;

    void note(Vec2 pos, float dt) { occupancy_[cellOf(pos)] += dt; }

    float occupancy(int i) const { return occupancy_[i]; }
    float total() const {
        float t = 0.f;
        for (float o : occupancy_) t += o;
        return t;
    }

    // 1.0 when the player has been in one cell 60%+ of the observed time and
    // at least `minSeconds` have been observed.
    bool concentrated(float minSeconds = 30.f, float share = 0.6f) const {
        const float t = total();
        if (t < minSeconds) return false;
        float mx = 0.f;
        for (float o : occupancy_) mx = std::max(mx, o);
        return mx / t >= share;
    }

    void decay(float factor) { for (auto& o : occupancy_) o *= factor; }

private:
    int cellOf(Vec2 p) const {
        const float cx = (p.x + kExtent * 0.5f) / kExtent; // 0..1
        const float cz = (p.z + kExtent * 0.5f) / kExtent;
        const int ix = clamp(int(cx * kCells), 0, kCells - 1);
        const int iz = clamp(int(cz * kCells), 0, kCells - 1);
        return iz * kCells + ix;
    }

    std::array<float, kCells * kCells> occupancy_{};
};

} // namespace rcai
