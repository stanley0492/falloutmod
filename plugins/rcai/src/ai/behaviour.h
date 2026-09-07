#pragma once
// P0-b2 · Behaviour layer: utility AI over the engine's package system.
//
// The brain never deletes the engine state machine — it rewrites the *combat
// package tick*. For each actor a set of candidate goals is scored against
// world state; the winner is emitted as an Action. In-game the F4SE adapter
// translates Actions into engine calls (re-issue MoveTo/Combat/EnterVehicle
// packages + direct steering); headlessly, sim.h executes them directly.
//
// Goal weights come from the archetype CombatStyle (combat_styles.h), scaled
// by the adaptive-difficulty governor (anti_cheese.h).

#include "ai/anti_cheese.h"
#include "ai/cover.h"
#include "ai/combat_styles.h"
#include "ai/perception.h"
#include "ai/squad.h"
#include "ai/threat.h"
#include "world/world.h"

namespace rcai {

class Brain {
public:
    BrainResult tick(WorldSnapshot& w, const std::vector<WorldEvent>& events, float dt) {
        BrainResult out;

        // ---- 1. fold world events into memory -----------------------------
        for (const auto& ev : events) {
            switch (ev.kind) {
            case WorldEvent::Kind::ShotFired:
                if (!ev.actorId.empty() && ev.actorId == "player") {
                    antiCheese_.onPlayerShot(w, ev.hit);
                    if (ev.hit) antiCheese_.onPlayerDamage(w, ev.value);
                    if (!ev.otherId.empty()) {
                        if (const auto* a = w.findActor(ev.otherId))
                            updateThreat(w, const_cast<ActorState&>(*a), dt, true, ev.hit);
                    }
                } else if (!ev.actorId.empty()) {
                    // An enemy shot: allies learn the player is hostile.
                    alertAll(w, ev.pos, false);
                }
                break;
            case WorldEvent::Kind::ActorDied:
                // Squads rebuild next tick via update(); nothing to cache.
                break;
            case WorldEvent::Kind::NoiseMade:
                for (auto& a : w.actors)
                    if (a.health > 0.f && distance(a.pos, ev.pos) <= 30.f * ev.value)
                        a.suspicion = clamp(a.suspicion + 0.3f * ev.value, 0.f, 1.f);
                break;
            default:
                break;
            }
        }
        habits_.note(w.player.pos, dt);
        habits_.decay(1.f - 0.001f * dt); // very slow forgetting
        const bool engaged = anyInCombat(w);
        const Vec2 probePos = antiCheese_.tick(w, habits_, engaged);
        if (antiCheese_.state().probeRequested) alertAll(w, probePos, false);

        // ---- 2. per-actor decide -------------------------------------------
        for (auto& a : w.actors) {
            if (a.health <= 0.f) continue;
            decide(w, a, dt, out);
        }
        return out;
    }

    const AntiCheeseState& antiCheese() const { return antiCheese_.state(); }
    const PlayerHabitGrid& habits() const { return habits_; }

    std::string debug(const WorldSnapshot& w) const {
        char buf[256];
        const int inCombat = countInCombat(w);
        std::snprintf(buf, sizeof(buf),
                      "brain: t=%.1f actors=%d inCombat=%d groups=%zu %s",
                      w.time, int(w.actors.size()), inCombat, squads_.groupCount(),
                      antiCheese_.state().describe().c_str());
        return buf;
    }

private:
    SquadCoordinator squads_;
    AntiCheese antiCheese_;
    PlayerHabitGrid habits_;
    // Per-actor sensory profile cache (keyed by id) — archetype-based.
    std::map<std::string, SensoryProfile> sensoryCache_;

    SensoryProfile& sensory(const WorldSnapshot& w, const ActorState& a) {
        auto it = sensoryCache_.find(a.id);
        if (it != sensoryCache_.end()) return it->second;
        SensoryProfile p;
        const std::string arch = a.factionIndex >= 0 && a.factionIndex < int(w.factions.size())
                                     ? w.factions[a.factionIndex].archetype
                                     : std::string("generic");
        const CombatStyle s = styleForArchetype(arch);
        if (arch == "sniper") {
            p.sightRange = 90.f;
            p.sightConeDeg = 90.f;
        } else if (arch == "heavy") {
            p.sightRange = 38.f;
        }
        p.perceptionMult = 1.0f;
        (void)s;
        return sensoryCache_.emplace(a.id, p).first->second;
    }

    bool anyInCombat(const WorldSnapshot& w) const {
        for (const auto& a : w.actors)
            if (a.health > 0.f && a.inCombat) return true;
        return false;
    }

    int countInCombat(const WorldSnapshot& w) const {
        int n = 0;
        for (const auto& a : w.actors)
            if (a.health > 0.f && a.inCombat) ++n;
        return n;
    }

    void alertAll(const WorldSnapshot& w, Vec2 pos, bool) {
        for (const auto& a : w.actors)
            if (a.health > 0.f && a.inCombat) {
                squads_.alert(w, a.id, pos);
                break; // one broadcast per tick is enough
            }
    }

    // ---- goal evaluation for one actor --------------------------------------

    struct Goal {
        Action::Type type = Action::Type::Idle;
        float score = 0.f;
        Vec2 at{0, 0};
        std::string targetId;
    };

    void decide(const WorldSnapshot& w, ActorState& a, float dt, BrainResult& out) {
        const std::string arch = a.factionIndex >= 0 && a.factionIndex < int(w.factions.size())
                                     ? w.factions[a.factionIndex].archetype
                                     : std::string("generic");
        CombatStyle style = styleForArchetype(arch);
        style.aggression = clamp(style.aggression * antiCheese_.state().aggressionScale, 0.f, 1.f);
        style.fleeThreshold = clamp(style.fleeThreshold + antiCheese_.state().fleeShift, 0.3f, 0.95f);
        const float flankBoost = antiCheese_.state().flankBoost;

        // --- perception & combat state transition ---------------------------
        const PerceptionResult pr = perceive(w, a, sensory(w, a), dt);
        const float hpFrac = a.maxHealth > 0.f ? a.health / a.maxHealth : 0.f;

        if (!a.inCombat) {
            if (pr.sawPlayer && a.detection >= sensory(w, a).detectionCombatThreshold) {
                a.inCombat = true;
                a.package = "Combat";
                squads_.alert(w, a.id, w.player.pos);
            }
        }

        const int gi = squads_.update(w, a.id);
        const Role role = gi >= 0 ? squads_.group(gi)->roleOf(a.id) : Role::None;
        const bool hasAlert = gi >= 0 && squads_.group(gi)->alert.valid() &&
                              (w.time - squads_.group(gi)->alert.time) < squads_.group(gi)->alert.ttl;

        // A squad alert puts non-combat members on high alert: they pre-
        // position in cover instead of standing in the open.
        if (hasAlert && !a.inCombat)
            a.suspicion = std::max(a.suspicion, 0.7f);

        // --- utility scores --------------------------------------------------
        std::vector<Goal> goals;

        const float dist = distance(a.pos, w.player.pos);
        const bool canSeePlayer = w.hasLOS(a.pos, w.player.pos);
        const bool exposed = canSeePlayer && a.inCombat;
        const float hpBelowFlee = a.inCombat && hpFrac < style.fleeThreshold;
        const float recency = clamp(1.f - lastSeenAge(w, a) / 30.f, 0.f, 1.f);

        if (!a.inCombat) {
            // Cautious archetypes pre-position in cover before contact
            // (the stock engine never does this; real squads take up
            // ambush positions when they hear something).
            if (a.suspicion > 0.4f && style.caution > 0.45f && w.coverPoints.size() > 0) {
                std::vector<Vec2> threats{w.player.pos};
                threats.insert(threats.end(), w.playerTrail.begin(), w.playerTrail.end());
                const CoverCandidate cc = bestCover(w, a, threats, a.speed * 6.0f);
                if (cc.score > 0.2f) {
                    goals.push_back({Action::Type::TakeCover, style.caution * 0.9f, cc.pos, ""});
                }
            }
            // Suspicion-driven search.
            if (a.suspicion > 0.35f) {
                const Vec2 dest = hasAlert ? squads_.group(gi)->alert.pos : a.lastSeenPlayer;
                goals.push_back({Action::Type::Search, a.suspicion * (0.4f + 0.6f * recency), dest, ""});
            }
            goals.push_back({Action::Type::Patrol, 0.15f, a.pos, ""});
        } else {
            const float engageScore =
                canSeePlayer && dist <= style.engageMax && dist >= style.engageMin * 0.5f
                    ? style.aggression * (1.f - 0.5f * clamp(dist / (style.engageMax + 1e-6f), 0.f, 1.f))
                    : 0.2f * style.aggression; // still wants to close range
            if (a.ammo > 0.f && a.reloadTimer <= 0.f)
                goals.push_back({Action::Type::Attack, engageScore, w.player.pos, "player"});

            // Suppress: pin the player, favoured while holding cover.
            if (a.hasCover && canSeePlayer)
                goals.push_back({Action::Type::Suppress, 0.35f * style.caution + 0.25f * (role == Role::Suppressor ? 1.f : 0.f),
                                 w.player.pos, "player"});

            // Committed to a cover point: keep the goal even once the route
            // loses sight of the player (the point is in the wall's shadow).
            if (a.enRouteToCover && !a.hasCover) {
                a.enRouteTime += dt;
                if (a.enRouteTime < 6.f) {
                    goals.push_back({Action::Type::TakeCover, 0.95f, a.moveTarget, ""});
                } else {
                    a.enRouteToCover = false; // unreachable; abandon
                }
            }

            // Take cover when exposed.
            if (exposed && !a.hasCover && !a.enRouteToCover && w.coverPoints.size() > 0) {
                std::vector<Vec2> threats{w.player.pos};
                threats.insert(threats.end(), w.playerTrail.begin(), w.playerTrail.end());
                const CoverCandidate cc = bestCover(w, a, threats, a.speed * 4.0f);
                if (cc.score > 0.25f)
                    goals.push_back({Action::Type::TakeCover, style.coverPreference * 0.9f * (1.f + (1.f - a.coverQuality)),
                                     cc.pos, ""});
            }

            // Flank: designated flankers leave cover to attack the player's
            // blind side; disciplined archetypes hold their suppressive
            // position (this is what makes the cover/peek cycle work as a
            // team tactic instead of everyone running around).
            {
                const Vec2 toPlayer = normalize(w.player.pos - a.pos);
                const float rel = std::fabs(angleBetween(toPlayer, normalize(w.player.facing)));
                const float inFront = rel < 0.6f ? 1.f : 0.3f; // radians
                const bool undisciplined = style.discipline < 0.5f && style.flankBias > 0.4f;
                if (role == Role::Flanker || undisciplined) {
                    // Team rule: flank only while enough of the group holds
                    // suppressing positions; otherwise keep the cover.
                    float coverRatio = 1.f;
                    if (gi >= 0) {
                        const auto* g = squads_.group(gi);
                        int alive = 0, cov = 0;
                        for (const auto& m : g->members)
                            if (const auto* m0 = w.findActor(m)) {
                                if (m0->health > 0.f) { ++alive; if (m0->hasCover) ++cov; }
                            }
                        coverRatio = alive > 0 ? float(cov) / alive : 1.f;
                    }
                    const float suppressionGate = coverRatio > 0.5f ? 1.f : 0.3f;
                    const Vec2 fp = SquadCoordinator::flankPosition(w.player.pos, w.player.facing, a.pos);
                    goals.push_back({Action::Type::Flank,
                                     style.flankBias * 0.7f * inFront * flankBoost * suppressionGate, fp, ""});
                }
            }

            // Retreat / flee.
            if (hpBelowFlee) {
                const float fleeW = 0.5f + 0.5f * (1.f - hpFrac / style.fleeThreshold) + 0.4f * a.threat;
                goals.push_back({Action::Type::Retreat, fleeW * style.caution, retreatPoint(a), ""});
                if (hpFrac < 0.25f || a.threat > 0.8f)
                    goals.push_back({Action::Type::Flee, fleeW * 1.2f, retreatPoint(a) * 2.5f, ""});
            }

            // Medic aid.
            if (role == Role::Medic) {
                for (const auto& b : w.actors) {
                    if (b.health <= 0.f || b.id == a.id || b.factionIndex != a.factionIndex) continue;
                    const float bFrac = b.maxHealth > 0.f ? b.health / b.maxHealth : 1.f;
                    if (bFrac < 0.6f && distance(a.pos, b.pos) < 60.f) {
                        goals.push_back({Action::Type::AidAlly, 0.8f * (1.f - bFrac), b.pos, b.id});
                        break;
                    }
                }
            }

            // Leader calls reinforcements when the group is bleeding out.
            if (role == Role::Leader && gi >= 0) {
                const auto* g = squads_.group(gi);
                float hpSum = 0.f, hpMax = 0.f;
                int alive = 0;
                for (const auto& m : g->members) {
                    if (const auto* m0 = w.findActor(m)) {
                        if (m0->health > 0.f) { ++alive; hpSum += m0->health; hpMax += m0->maxHealth; }
                    }
                }
                if (alive >= 2 && hpMax > 0.f && hpSum / hpMax < 0.55f)
                    goals.push_back({Action::Type::CallReinforcements, 0.6f, a.pos, ""});
            }
        }

        // Reload trumps most goals but not flee.
        if (a.inCombat && a.ammo <= 0.f && a.reloadTimer <= 0.f) {
            a.reloadTimer = 2.5f;
            goals.push_back({Action::Type::Reload, 1.5f, a.pos, ""});
        }

        // ---- pick winner ------------------------------------------------------
        Goal best{Action::Type::Idle, 0.05f, a.pos, ""};
        for (const auto& g : goals)
            if (g.score > best.score) best = g;

        applyGoal(w, a, best, style, dt);
        if (!best.targetId.empty() && best.type == Action::Type::Attack)
            a.threat = clamp(a.threat, 0.f, 1.f); // no-op keeps API stable

        // ---- emit action -------------------------------------------------------
        Action act;
        act.actorId = a.id;
        act.type = best.type;
        act.target = best.at;
        act.targetActorId = best.targetId;
        act.param = clamp(best.score, 0.f, 1.f);
        out.actions.push_back(act);
    }

    void applyGoal(const WorldSnapshot& w, ActorState& a, const Goal& g, const CombatStyle& style, float dt) {
        (void)w; (void)dt;
        switch (g.type) {
        case Action::Type::MoveTo:
        case Action::Type::Search:
        case Action::Type::Flank:
        case Action::Type::AidAlly:
            a.moveTarget = g.at;
            a.moving = true;
            a.package = actionPackageName(g.type);
            a.enRouteToCover = false;
            break;
        case Action::Type::Retreat:
        case Action::Type::Flee:
            a.moveTarget = g.at;
            a.moving = true;
            a.package = actionPackageName(g.type);
            a.enRouteToCover = false; // disengaging abandons cover plans
            break;
        case Action::Type::TakeCover:
            a.moveTarget = g.at;
            a.moving = true;
            a.package = actionPackageName(g.type);
            a.enRouteToCover = true;
            a.enRouteTime = 0.f;
            if (distance(a.pos, g.at) <= 1.5f) {
                a.hasCover = true;
                a.coverPos = g.at;
                a.coverQuality = 0.7f;
                a.peekTimer = style.peekDuration;
                a.moving = false;
                a.enRouteToCover = false;
            }
            break;
        case Action::Type::Attack:
        case Action::Type::Suppress:
            // Enemies holding cover hold position and fire from their peek
            // window instead of walking into the open.
            if (!a.hasCover && distance(a.pos, g.at) > 2.f) {
                a.moveTarget = g.at;
                a.moving = true;
            } else {
                a.moving = false;
            }
            a.package = "Combat";
            break;
        case Action::Type::Patrol:
            a.package = "Patrol";
            break;
        case Action::Type::Reload:
            a.package = "Combat";
            break;
        case Action::Type::CallReinforcements:
            a.package = "Combat";
            break;
        default:
            a.moving = false;
            break;
        }
    }

    static Vec2 retreatPoint(const ActorState& a) {
        // Fall back ~40 m away from the player.
        const Vec2 away = normalize(a.pos - a.lastSeenPlayer);
        return a.pos + away * 40.f;
    }

    static const char* actionPackageName(Action::Type t) {
        switch (t) {
        case Action::Type::Search: return "MoveTo";
        case Action::Type::Flank: return "MoveTo";
        case Action::Type::Retreat: return "Flee";
        case Action::Type::Flee: return "Flee";
        case Action::Type::AidAlly: return "Follow";
        case Action::Type::TakeCover: return "MoveTo";
        default: return "Combat";
        }
    }
};

} // namespace rcai
