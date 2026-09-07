#pragma once
// P0-b1 · Perception module.
//
// Replaces the stock engine's GMST-driven detection (149 sensory GMSTs in the
// decompiled set) with per-actor sensory profiles:
//
//   sight  : cone + range * light * perceptionMult + line of sight +
//            sneak discount
//   hearing: distance scaled by noise loudness, occluded by walls
//   smell  : chem/aura radius (bench: unused; in-game: rad/chem states)
//
// Output is a continuous evidence value in [0,1] plus a memory stamp
// (last-seen position/time) consumed by the behaviour layer.

#include "world/world.h"

namespace rcai {

struct SensoryProfile {
    float sightRange = 45.f;     // metres at lightLevel 1.0
    float sightConeDeg = 120.f;  // full cone angle
    float hearingRange = 60.f;   // at loudness 1.0 (sprint ~ 48 m)
    float smellRange = 8.f;
    float sightDecayPerSec = 0.25f;   // detection fades when no new evidence
    float suspicionDecayPerSec = 0.04f;
    float detectionCombatThreshold = 0.65f; // triggers combat state
    float perceptionMult = 1.0f;  // master multiplier (RCAI.ini fPerceptionMult)
};

struct PerceptionResult {
    float sightEvidence = 0.f;   // 0..1 this tick
    bool sawPlayer = false;
    bool heardPlayer = false;
    bool smellPlayer = false;
    Vec2 sightPos{0, 0};
};

// One perception tick for `actor` against the world.
// Mutates actor.detection / suspicion / lastSeen* / lastHeardTime in place.
inline PerceptionResult perceive(const WorldSnapshot& w, ActorState& actor,
                                 const SensoryProfile& prof, float dt) {
    PerceptionResult r;
    const ActorState& pl = w.player;
    const float dist = distance(actor.pos, pl.pos);

    // --- sight -------------------------------------------------------------
    {
        const Vec2 to = normalize(pl.pos - actor.pos);
        const float facing = dot(normalize(actor.facing), to);
        const float halfCone = std::cos((prof.sightConeDeg * 0.5f) * 3.14159265f / 180.f);
        const bool inCone = facing > halfCone;

        float range = prof.sightRange * (0.35f + 0.65f * w.lightLevel) * prof.perceptionMult;
        if (w.playerFlashlightOn) range *= 1.35f; // flashlight makes the player easier to spot
        // Sneak discount: moving *quietly* (0 < noise < 0.25) is harder to spot
        // than moving loudly; a fully still target is easy to spot (noise == 0).
        if (w.playerNoise > 0.0f && w.playerNoise < 0.25f) range *= 0.5f;

        bool clear = dist <= range && inCone && w.hasLOS(actor.pos, pl.pos);
        if (clear) {
            r.sawPlayer = true;
            // Evidence grows with proximity (closer = faster lock).
            const float proximity = 1.f - clamp(dist / range, 0.f, 1.f);
            r.sightEvidence = 0.25f + 0.75f * proximity;
            r.sightPos = pl.pos;
        }
        if (r.sawPlayer) {
            actor.detection = clamp(actor.detection + r.sightEvidence * dt * 2.5f, 0.f, 1.f);
            actor.suspicion = clamp(actor.suspicion + r.sightEvidence * dt * 1.5f, 0.f, 1.f);
            actor.lastSeenPlayer = pl.pos;
            actor.lastSeenTime = w.time;
        } else {
            actor.detection = clamp(actor.detection - prof.sightDecayPerSec * dt, 0.f, 1.f);
        }
    }

    // --- hearing -----------------------------------------------------------
    {
        if (w.playerNoise > 0.05f) {
            const float hearRange = prof.hearingRange * w.playerNoise * prof.perceptionMult;
            const bool heard = dist <= hearRange && w.hasLOS(actor.pos, pl.pos);
            if (heard) {
                r.heardPlayer = true;
                actor.suspicion = clamp(actor.suspicion + dt * 1.2f * w.playerNoise, 0.f, 1.f);
                actor.lastHeardTime = w.time;
            }
        }
    }

    // --- smell (placeholder model; in-game: chem/radiation auras) -----------
    if (dist <= prof.smellRange * prof.perceptionMult) {
        r.smellPlayer = true;
        actor.suspicion = clamp(actor.suspicion + dt * 0.15f, 0.f, 1.f);
    }

    // Slow decay of suspicion when no input.
    if (!r.sawPlayer && !r.heardPlayer)
        actor.suspicion = clamp(actor.suspicion - prof.suspicionDecayPerSec * dt, 0.f, 1.f);

    return r;
}

} // namespace rcai
