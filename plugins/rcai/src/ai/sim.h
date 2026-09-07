#pragma once
// Headless simulation of the RCAI stack: the Brain decides, this executor
// applies movement/fire/cover/heal effects, and a scripted PlayerModel
// provides the adversary. This is the same action contract the in-game F4SE
// adapter honours — running it headlessly is what lets the whole AI stack be
// unit-tested and benchmarked without the game (milestone M1/M2 acceptance).

#include <algorithm>
#include <cmath>
#include <deque>

#include "ai/behaviour.h"
#include "util/rng.h"

namespace rcai {

struct Shot {
    std::string shooterId;
    Vec2 from{0, 0};
    Vec2 to{0, 0};
    bool fromCover = false;
    bool suppress = false;
    bool hit = false;
    float damage = 0.f;
    float range = 0.f;
};

struct SimStats {
    int enemyShots = 0;
    int enemyShotsFromCover = 0;
    int enemyShotsFlanking = 0; // |relative angle| > 60 deg at time of shot
    std::vector<std::string> flankers; // distinct actors that flanked
    int playerShots = 0;
    int playerShotsHit = 0;
    float playerDamageDealt = 0.f;
    int firstKillTick = -1;
    int heals = 0;
    int reinforcementsCalled = 0;
    int totalKills = 0;
    int enemyDeaths = 0;
    double combatTime = 0;      // seconds any enemy is in combat
    double coverCombatTime = 0; // seconds any enemy is in combat AND in cover
};

// Scripted benchmark player: advances, strafes at close range, auto-fires.
static constexpr float kPeekDist = 8.f;

struct PlayerModel {
    float fireInterval = 0.8f;
    float damage = 15.f;
    float accuracy = 0.7f;
    float advanceSpeed = 2.5f;
    float lastShot = -999.f;
    std::string targetId; // focus fire: hold the target until it dies or
                          // breaks line of sight (a real shooter's habit)
};

class HeadlessSim {
public:
    HeadlessSim(WorldSnapshot w, std::uint64_t seed = 1) : w_(std::move(w)), rng_(seed) {
        w_.player.isPlayer = true;
    }

    WorldSnapshot& world() { return w_; }
    SimStats& stats() { return stats_; }
    Brain& brain() { return brain_; }
    PlayerModel& playerModel() { return pm_; }
    int tickIndex() const { return tickIndex_; }

    void step(float dt) {
        w_.time += dt;
        // Player trail: memory for cover evaluation (0.5 s resolution, ~6 s).
        if (w_.playerTrail.empty() ||
            distance(w_.playerTrail.back(), w_.player.pos) > 2.f) {
            w_.playerTrail.push_back(w_.player.pos);
            if (w_.playerTrail.size() > 6) w_.playerTrail.erase(w_.playerTrail.begin());
        }
        // 1) Enemies: fold previous-tick events, decide, act.
        std::vector<WorldEvent> events = pending_;
        for (const auto& a : w_.actors)
            if (a.health <= 0.f)
                events.push_back({WorldEvent::Kind::ActorDied, a.id, {}, 0.f, false});
        const BrainResult br = brain_.tick(w_, events, dt);
        for (const auto& act : br.actions) {
            if (act.type == Action::Type::CallReinforcements) ++stats_.reinforcementsCalled;
            applyEnemyAction(act, dt);
        }

        // 2) Player acts; its events feed the enemies next tick (causality).
        pending_ = stepPlayer(dt);

        // 3) First-kill + combat-time-in-cover bookkeeping.
        if (stats_.totalKills > 0 && stats_.firstKillTick < 0)
            stats_.firstKillTick = tickIndex_;
        for (const auto& a : w_.actors) {
            if (a.health <= 0.f || !a.inCombat) continue;
            stats_.combatTime += dt;
            if (a.hasCover) stats_.coverCombatTime += dt;
        }
        ++tickIndex_;
    }

private:
    void applyEnemyAction(const Action& act, float dt) {
        auto* a = w_.findActor(act.actorId);
        if (!a || a->health <= 0.f) return;

        if (a->reloadTimer > 0.f) {
            a->reloadTimer -= dt;
            if (a->reloadTimer <= 0.f) a->ammo = 30.f;
        }
        // Leaving the cover point (retreat, aid, flank) ends the cover state.
        if (a->hasCover && distance(a->pos, a->coverPos) > 2.f)
            a->hasCover = false;
        // Peek/hide cycle: positive = peek window counting down, negative =
        // hide phase counting up. (Cover duty cycle ~60%.)
        if (a->hasCover && a->inCombat) {
            if (a->peekTimer > 0.f) {
                a->peekTimer -= dt;
                if (a->peekTimer <= 0.f) a->peekTimer = -1.2f; // tuck back in
            } else {
                a->peekTimer += dt;
                if (a->peekTimer >= 0.f) a->peekTimer = 1.2f;  // lean out again
            }
        }

        // Movement. (The 5 m snap on a cover approach abstracts navmesh
        // routing around the occluder — the in-game build walks the actual
        // navmesh path; see INTEGRATION_CHECKLIST.md.)
        if (a->moving && distance(a->pos, a->moveTarget) > 0.5f) {
            const float stepLen = a->speed * dt;
            const Vec2 dir = normalize(a->moveTarget - a->pos);
            const bool covering = act.type == Action::Type::TakeCover;
            if (covering && distance(a->pos, a->moveTarget) < 5.f) {
                a->pos = a->moveTarget; // routed around the occluder
            } else if (stepLen < distance(a->pos, a->moveTarget)) {
                a->pos = a->pos + dir * stepLen;
            } else {
                a->pos = a->moveTarget;
            }
            a->facing = dir;
            if (act.type == Action::Type::TakeCover && distance(a->pos, a->moveTarget) <= 1.5f) {
                a->hasCover = true;
                a->coverPos = a->pos;
                a->coverQuality = 0.7f;
                a->peekTimer = 1.2f;
                a->moving = false;
            }
            if (act.type == Action::Type::Flee) {
                const Vec2 p = a->pos;
                if (p.x < -200.f || p.x > 200.f || p.z < -200.f || p.z > 200.f)
                    a->health = 0.f; // escaped the simulated cell
            }
        }

        // Firing. Enemies holding cover fire from a *peek position* (step
        // out ~8 m toward the player) during their peek window — the 2D
        // abstraction of walking to the occluder's edge and leaning out.
        const bool wantsFire =
            (act.type == Action::Type::Attack || act.type == Action::Type::Suppress) &&
            a->inCombat && a->reloadTimer <= 0.f && a->ammo > 0.f;
        if (wantsFire) {
            const bool peeking = !a->hasCover || a->peekTimer > 0.f;
            Vec2 firePos = a->pos;
            if (a->hasCover)
                firePos = a->pos + normalize(w_.player.pos - a->pos) * kPeekDist;
            const float dist = distance(firePos, w_.player.pos);
            if (peeking && (w_.time - a->lastShotTime >= a->fireInterval) && dist <= 90.f &&
                w_.hasLOS(firePos, w_.player.pos)) {
                const float q = dist / 25.f;
                const float falloff = 1.f / (1.f + q * q);
                const float pHit = a->accuracy * falloff * (act.type == Action::Type::Suppress ? 0.6f : 1.f);
                const bool hit = rng_.chance(pHit);
                Shot s{a->id, firePos, w_.player.pos, a->hasCover,
                       act.type == Action::Type::Suppress, hit,
                       hit ? a->weaponDamage : 0.f, dist};
                ++stats_.enemyShots;
                if (a->hasCover) ++stats_.enemyShotsFromCover;
                // Flank = shooter is more than 60 deg off the player's facing.
                const float relAngle =
                    std::fabs(angleBetween(w_.player.facing, normalize(a->pos - w_.player.pos)));
                if (relAngle > 1.047f) {
                    ++stats_.enemyShotsFlanking;
                    if (std::find(stats_.flankers.begin(), stats_.flankers.end(), a->id) ==
                        stats_.flankers.end())
                        stats_.flankers.push_back(a->id);
                }
                if (hit) w_.player.health -= s.damage;
                lastEnemyShot = s.from;
                lastEnemyShotTime = w_.time;
                a->lastShotTime = w_.time;
                --a->ammo;
                if (a->hasCover) a->peekTimer = -0.8f; // quick tuck after the shot
                a->facing = normalize(w_.player.pos - a->pos);
            }
        }

        // Medic heal on contact.
        if (act.type == Action::Type::AidAlly && !act.targetActorId.empty()) {
            if (auto* b = w_.findActor(act.targetActorId)) {
                if (b->health > 0.f && b->health < b->maxHealth && distance(a->pos, b->pos) < 3.f) {
                    b->health = std::min(b->maxHealth, b->health + 8.f);
                    ++stats_.heals;
                }
            }
        }
    }

    std::vector<WorldEvent> stepPlayer(float dt) {
        std::vector<WorldEvent> evs;
        const ActorState& pl = w_.player;
        if (pl.health <= 0.f) return evs;

        ActorState* target = nullptr;
        float bestD = 1e9f;
        // Hold the current target while it is alive and visible (focus fire).
        if (!pm_.targetId.empty()) {
            auto* t = w_.findActor(pm_.targetId);
            if (t && t->health > 0.f && w_.hasLOS(pl.pos, t->pos)) {
                target = t;
                bestD = distance(pl.pos, t->pos);
            } else {
                pm_.targetId.clear();
            }
        }
        if (!target) {
            for (auto& a : w_.actors) {
                if (a.health <= 0.f) continue;
                const float d = distance(pl.pos, a.pos);
                if (d < bestD && w_.hasLOS(pl.pos, a.pos)) {
                    bestD = d;
                    target = &a;
                }
            }
            if (target) pm_.targetId = target->id;
        }
        if (!target) {
            // Stalemate breaker: if we were recently shot, move toward the
            // last shot (a real player flanks toward the shooter).
            if (w_.time - lastEnemyShotTime < 5.f && length(lastEnemyShot - w_.player.pos) > 1.f) {
                w_.player.facing = normalize(lastEnemyShot - w_.player.pos);
                w_.player.pos = w_.player.pos + w_.player.facing * 3.f * dt;
                w_.playerNoise = 0.6f;
            } else {
                w_.playerNoise = 0.3f;
            }
            return evs;
        }

        w_.player.facing = normalize(target->pos - pl.pos);
        const bool close = bestD < 25.f;
        Vec2 move{0, 0};
        if (!close) {
            move = w_.player.facing * pm_.advanceSpeed * dt;
        } else {
            const float s = std::sin(w_.time * 0.8f) > 0.f ? 1.f : -1.f;
            move = Vec2{-w_.player.facing.z, w_.player.facing.x} * pm_.advanceSpeed * dt * s;
        }
        w_.player.pos = w_.player.pos + move;
        w_.playerNoise = close ? 0.5f : 0.8f;

        if (w_.time - pm_.lastShot >= pm_.fireInterval) {
            pm_.lastShot = w_.time;
            ++stats_.playerShots;
            const float q = bestD / 30.f;
            const bool hit = rng_.chance(pm_.accuracy / (1.f + q * q));
            if (hit) {
                ++stats_.playerShotsHit;
                const float dmg = pm_.damage * (0.8f + 0.4f * rng_.nextFloat());
                stats_.playerDamageDealt += dmg;
                target->health -= dmg;
                if (target->health <= 0.f) {
                    target->health = 0.f;
                    ++stats_.totalKills;
                    ++stats_.enemyDeaths;
                }
            }
            evs.push_back({WorldEvent::Kind::ShotFired, "player", target->id, w_.player.pos,
                           hit ? pm_.damage : 0.f, hit});
        }
        return evs;
    }

    // Where the last enemy shot came from — the player model uses it to
    // break stalemates (a real player moves toward the last hit).
    Vec2 lastEnemyShot{0, 0};
    float lastEnemyShotTime = -99.f;

    WorldSnapshot w_;
    Brain brain_;
    PlayerModel pm_;
    Rng rng_;
    SimStats stats_;
    std::vector<WorldEvent> pending_;
    int tickIndex_ = 0;
};

} // namespace rcai
