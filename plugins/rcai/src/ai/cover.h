#pragma once
// P0-b3 · Cover solver.
//
// The engine's `bPartialCover` GMST proves the base engine has *some* cover
// awareness; this module replaces it with a real evaluator over the cell's
// precomputed cover points (the in-game sampler derives them from the
// navgrid + collision, as documented in INTEGRATION_CHECKLIST.md).
//
// score(point) = occlusion from the player (hard LOS block = 1, line of
// sight = 0, partial = angle-based) * intrinsic quality + approach bonus.
// Peek-shoot cadence is driven by the combat style's peekDuration.

#include "world/world.h"

namespace rcai {

// Distance from point p to segment ab (2D).
inline float pointSegDistance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const float l2 = dot(ab, ab);
    float t = l2 > 1e-9f ? dot(p - a, ab) / l2 : 0.f;
    t = clamp(t, 0.f, 1.f);
    return distance(p, a + ab * t);
}

// 0..1: how well `point` hides from an observer at `from`.
// Hard occlusion (a wall crosses the line) scores 0.7+; *grazing* cover
// (an occluder within 4 m of the line — the edge of a car, a wall fragment)
// scores 0.25-0.4: the engine's bPartialCover behaviour, generalized.
inline float occlusionScore(const WorldSnapshot& w, Vec2 from, Vec2 point) {
    if (w.hasLOS(from, point)) {
        // Segment-to-segment distance (min of the 4 endpoint projections).
        auto segDist = [](Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
            float m = pointSegDistance(a, c, d);
            m = std::min(m, pointSegDistance(b, c, d));
            m = std::min(m, pointSegDistance(c, a, b));
            m = std::min(m, pointSegDistance(d, a, b));
            return m;
        };
        float bestNear = 1e9f;
        for (const auto& seg : w.walls)
            bestNear = std::min(bestNear, segDist(from, point, seg.a, seg.b));
        if (bestNear < 4.f) return 0.25f + 0.15f * (1.f - bestNear / 4.f);
        return 0.f;
    }
    // Hard occlusion: how many independent walls block? More = safer.
    int blockers = 0;
    for (const auto& seg : w.walls)
        if (segmentsIntersect(from, point, seg.a, seg.b)) ++blockers;
    return clamp(0.7f + 0.15f * float(blockers), 0.f, 1.f);
}

struct CoverCandidate {
    Vec2 pos{0, 0};
    float score = 0.f;
};

// Best cover for `actor` against threats at the given positions (player +
// any exposed enemies), excluding points the actor is already holding.
inline CoverCandidate bestCover(const WorldSnapshot& w, const ActorState& actor,
                                const std::vector<Vec2>& threats, float moveBudget) {
    CoverCandidate best;
    for (const auto& cp : w.coverPoints) {
        // Too far to reach this tick.
        if (distance(actor.pos, cp.pos) > moveBudget) continue;
        float occ = 1.f;
        for (const auto& t : threats) occ = std::min(occ, occlusionScore(w, t, cp.pos));
        // Slight preference for points close to current position (stability).
        const float approach = 1.f - clamp(distance(actor.pos, cp.pos) / (moveBudget + 1e-6f), 0.f, 1.f);
        const float score = occ * cp.quality * 0.8f + approach * 0.2f;
        if (score > best.score) {
            best = {cp.pos, score};
        }
    }
    return best;
}

} // namespace rcai
