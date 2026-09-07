// RCAI — Reactive Combat AI
// F4SE World Adapter Implementation.

#include "f4se_sampler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "ai/combat_styles.h"
#include "util/json.h"

namespace rcai {

// Global engine accessors definition
namespace f4se {
    PlayerCharacter* g_playerInstance = nullptr;
    VirtualMachine* g_papyrusVM = nullptr;
}

F4SEWorldSampler::F4SEWorldSampler() = default;
F4SEWorldSampler::~F4SEWorldSampler() = default;

void F4SEWorldSampler::resetFrameStats() {
    diag_.losCallsPerFrame = 0;
}

bool F4SEWorldSampler::init(const std::string& combatStylesPath) {
    std::string path = combatStylesPath;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // Fallback search locations
        const std::vector<std::string> fallbacks = {
            "Data/RCAI/combat/combat_styles.json",
            "data/combat/combat_styles.json",
            "../RCAI/combat/combat_styles.json",
            "../../data/combat/combat_styles.json"
        };
        for (const auto& fb : fallbacks) {
            in.open(fb, std::ios::binary);
            if (in) {
                path = fb;
                break;
            }
        }
    }

    if (!in) {
        std::fprintf(stderr, "RCAI: warning: combat_styles.json could not be opened (%s)\n", combatStylesPath.c_str());
        diag_.initialized = true; // Still allow sampling with generic fallback
        return false;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string content = buffer.str();

    try {
        json::Value root = json::Value::parse(content);
        const json::Value* stylesVal = root.find("styles");
        if (stylesVal && stylesVal->isArray()) {
            for (const auto& item : stylesVal->asArray()) {
                const json::Value* edidVal = item.find("edid");
                const json::Value* formidVal = item.find("formid");
                const json::Value* archVal = item.find("archetype");

                if (archVal && archVal->isString()) {
                    const std::string& arch = archVal->asString();
                    if (edidVal && edidVal->isString()) {
                        styleEdidToArchetype_[edidVal->asString()] = arch;
                    }
                    if (formidVal && formidVal->isString()) {
                        const std::string& fidStr = formidVal->asString();
                        try {
                            std::uint32_t fid = static_cast<std::uint32_t>(std::stoul(fidStr, nullptr, 16));
                            styleFormIdToArchetype_[fid] = arch;
                        } catch (...) {}
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "RCAI: error parsing combat_styles.json: %s\n", e.what());
    }

    diag_.initialized = true;
    return !styleFormIdToArchetype_.empty() || !styleEdidToArchetype_.empty();
}

std::string F4SEWorldSampler::lookupArchetype(std::uint32_t formId, const std::string& edid) const {
    if (formId != 0) {
        auto it = styleFormIdToArchetype_.find(formId);
        if (it != styleFormIdToArchetype_.end()) {
            return it->second;
        }
    }
    if (!edid.empty()) {
        auto it = styleEdidToArchetype_.find(edid);
        if (it != styleEdidToArchetype_.end()) {
            return it->second;
        }
    }
    return "generic";
}

bool F4SEWorldSampler::sample(WorldSnapshot& out, float dt) {
    if (!diag_.initialized) {
        return false;
    }

    resetFrameStats();

    f4se::PlayerCharacter* player = testPlayer_ ? testPlayer_ : f4se::getPlayer();
    if (!player) {
        return false;
    }

    f4se::TESObjectCELL* cell = testCell_ ? testCell_ : player->parentCell;
    if (!cell) {
        return false;
    }

    diag_.inGameBound = true;

    // Integration Point 2: Player state
    if (!samplePlayer(player, out, dt)) {
        return false;
    }

    // Integration Point 3: Occluder geometry -> 2D wall segments
    if (!sampleOccluders(cell, out.player.pos, out)) {
        return false;
    }

    // Integration Point 1: Actor enumeration -> ActorState
    if (!sampleActors(cell, player, out)) {
        return false;
    }

    // Integration Point 4: Cover points generation & scoring
    generateCoverPoints(out);

    // Integration Point 6: Perception environmental parameters
    out.time += dt;
    out.lightLevel = player->isFlashlightOn ? 1.0f : (cell->isInterior() ? 0.4f : 0.8f);
    out.playerFlashlightOn = player->isFlashlightOn;
    out.playerNoise = player->stealthNoise;

    return true;
}

bool F4SEWorldSampler::samplePlayer(f4se::PlayerCharacter* player, WorldSnapshot& out, float /*dt*/) {
    try {
        out.player.isPlayer = true;
        out.player.id = "player";
        // 3D -> 2D x/z horizontal ground projection
        out.player.pos = Vec2{player->pos.x, player->pos.y};

        const float yaw = player->rot.z;
        out.player.facing = Vec2{std::cos(yaw), std::sin(yaw)};

        out.player.health = player->getActorValue(f4se::kActorValue_Health, 100.0f);
        out.player.maxHealth = 100.0f;
        out.player.speed = 3.5f;

        // Player trail memory contract (same as headless sim.h)
        if (out.playerTrail.empty() || distance(out.playerTrail.back(), out.player.pos) > 2.0f) {
            out.playerTrail.push_back(out.player.pos);
            if (out.playerTrail.size() > 6) {
                out.playerTrail.erase(out.playerTrail.begin());
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool F4SEWorldSampler::sampleOccluders(f4se::TESObjectCELL* cell, const Vec2& playerPos, WorldSnapshot& out) {
    out.walls.clear();
    diag_.occludersSampled = 0;

    if (cell->objectList.empty()) {
        return true;
    }

    try {
        for (f4se::UInt32 i = 0; i < cell->objectList.size(); ++i) {
            f4se::TESObjectREFR* refr = cell->objectList[i];
            if (!refr || refr->isDeleted() || refr->isDisabled() || !refr->baseForm) {
                continue;
            }

            const f4se::UInt8 fType = refr->baseForm->formType;
            if (fType == f4se::kFormType_STAT || fType == f4se::kFormType_MSTT || fType == f4se::kFormType_ACTI) {
                Vec2 center{refr->pos.x, refr->pos.y};
                const float dist = distance(center, playerPos);
                if (dist > 120.0f) {
                    continue; // Cull distant occluders outside combat engagement
                }

                const float yaw = refr->rot.z;
                const float halfWidth = 3.0f; // Nominal occluder span
                const Vec2 tangent{std::cos(yaw) * halfWidth, std::sin(yaw) * halfWidth};

                out.walls.push_back(WallSegment{center - tangent, center + tangent});
                ++diag_.occludersSampled;
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool F4SEWorldSampler::sampleActors(f4se::TESObjectCELL* cell, f4se::PlayerCharacter* player, WorldSnapshot& out) {
    out.actors.clear();
    diag_.actorsSampled = 0;

    if (cell->objectList.empty()) {
        return true;
    }

    try {
        for (f4se::UInt32 i = 0; i < cell->objectList.size(); ++i) {
            f4se::TESObjectREFR* refr = cell->objectList[i];
            if (!refr || refr->isDeleted() || refr->isDisabled() || !refr->baseForm) {
                continue;
            }

            const f4se::UInt8 fType = refr->formType;
            const f4se::UInt8 baseType = refr->baseForm ? refr->baseForm->formType : 0;
            if (fType != f4se::kFormType_ACHR && baseType != f4se::kFormType_NPC_) {
                continue;
            }

            f4se::Actor* actor = reinterpret_cast<f4se::Actor*>(refr);
            if (actor == player || actor->isDead()) {
                continue;
            }

            ActorState state;
            state.id = std::to_string(actor->formID);
            state.pos = Vec2{actor->pos.x, actor->pos.y};

            const float yaw = actor->rot.z;
            state.facing = Vec2{std::cos(yaw), std::sin(yaw)};

            state.health = actor->getActorValue(f4se::kActorValue_Health, 100.0f);
            state.maxHealth = 100.0f;
            state.inCombat = actor->isInCombat();

            // Archetype lookup from decompiled combat styles
            std::string arch = "generic";
            f4se::TESNPC* npc = actor->getNPC();
            if (npc && npc->combatStyle) {
                arch = lookupArchetype(npc->combatStyle->formID, "");
            } else if (npc && npc->templateNPC && npc->templateNPC->combatStyle) {
                arch = lookupArchetype(npc->templateNPC->combatStyle->formID, "");
            }

            CombatStyle cs = styleForArchetype(arch);
            state.speed = 3.0f + cs.aggression * 0.8f;
            state.accuracy = 0.5f + cs.discipline * 0.15f;
            state.weaponDamage = 10.0f;
            state.fireInterval = 0.8f + cs.caution * 0.8f;

            // Factions
            state.factionIndex = 0;
            if (actor->vendorFaction) {
                state.factionIndex = static_cast<int>(actor->vendorFaction->formID & 0xFF);
            }

            out.actors.push_back(state);
            ++diag_.actorsSampled;
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool F4SEWorldSampler::generateCoverPoints(WorldSnapshot& out) {
    out.coverPoints.clear();

    for (const auto& w : out.walls) {
        const Vec2 mid = (w.a + w.b) * 0.5f;
        const Vec2 dir = normalize(w.b - w.a);
        const Vec2 normal{-dir.z, dir.x};

        const Vec2 candidates[2] = {
            mid + normal * 2.0f,
            mid - normal * 2.0f
        };

        for (const auto& cand : candidates) {
            ++diag_.losCallsPerFrame;
            // Point is in cover if direct line of sight to current player is blocked
            if (!out.hasLOS(cand, out.player.pos)) {
                float quality = 0.75f;
                // Evaluate against recent player positions
                bool coversTrail = true;
                for (const auto& p : out.playerTrail) {
                    ++diag_.losCallsPerFrame;
                    if (out.hasLOS(cand, p)) {
                        coversTrail = false;
                        break;
                    }
                }
                if (coversTrail && !out.playerTrail.empty()) {
                    quality = 0.90f;
                }
                out.coverPoints.push_back(CoverPoint{cand, quality});
            }
        }
    }

    diag_.coverPointsPerCell = out.coverPoints.size();
    return true;
}

bool F4SEWorldSampler::checkLOS(const Vec2& from, const Vec2& to, const WorldSnapshot& snapshot) {
    ++diag_.losCallsPerFrame;
    return snapshot.hasLOS(from, to);
}

f4se::Actor* F4SEWorldSampler::findEngineActor(f4se::TESObjectCELL* cell, const std::string& id) {
    if (!cell || cell->objectList.empty() || id.empty()) {
        return nullptr;
    }
    try {
        const f4se::UInt32 formId = static_cast<f4se::UInt32>(std::stoul(id, nullptr, 10));
        for (f4se::UInt32 i = 0; i < cell->objectList.size(); ++i) {
            f4se::TESObjectREFR* refr = cell->objectList[i];
            if (refr && refr->formID == formId) {
                const f4se::UInt8 fType = refr->formType;
                const f4se::UInt8 baseType = refr->baseForm ? refr->baseForm->formType : 0;
                if (fType == f4se::kFormType_ACHR || baseType == f4se::kFormType_NPC_) {
                    return reinterpret_cast<f4se::Actor*>(refr);
                }
            }
        }
    } catch (...) {}
    return nullptr;
}

void F4SEWorldSampler::applyUtilityAction(f4se::Actor* actor, const Action& a) {
    try {
        f4se::AIUtilityPackage pkg;
        pkg.targetPos = f4se::NiPoint3{a.target.x, a.target.z, actor->pos.z};
        switch (a.type) {
            case Action::Type::Flank:
                pkg.actionType = f4se::AIUtilityPackage::kAction_Flank;
                break;
            case Action::Type::Retreat:
                pkg.actionType = f4se::AIUtilityPackage::kAction_Retreat;
                break;
            case Action::Type::AidAlly:
                pkg.actionType = f4se::AIUtilityPackage::kAction_AidAlly;
                if (!a.targetActorId.empty()) {
                    try { pkg.targetActorId = static_cast<f4se::UInt32>(std::stoul(a.targetActorId)); } catch (...) {}
                }
                break;
            default:
                pkg.actionType = f4se::AIUtilityPackage::kAction_MoveTo;
                break;
        }
        // Steer / pathing target updated safely on actor
        actor->pos.x = a.target.x;
        actor->pos.y = a.target.z;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "RCAI: warning in applyUtilityAction: %s\n", e.what());
    }
}

void F4SEWorldSampler::applyCombatAction(f4se::Actor* actor, const Action& a) {
    try {
        f4se::AICombatPackage pkg;
        pkg.actionType = (a.type == Action::Type::Suppress)
                             ? f4se::AICombatPackage::kCombat_Suppress
                             : f4se::AICombatPackage::kCombat_Attack;
        if (!a.targetActorId.empty()) {
            try { pkg.targetActorId = static_cast<f4se::UInt32>(std::stoul(a.targetActorId)); } catch (...) {}
        }
        pkg.faction = actor->vendorFaction;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "RCAI: warning in applyCombatAction: %s\n", e.what());
    }
}

void F4SEWorldSampler::applyFleeAction(f4se::Actor* actor, const Action& a) {
    try {
        f4se::AIFleePackage pkg;
        pkg.fleePoint = f4se::NiPoint3{a.target.x, a.target.z, actor->pos.z};
        pkg.fleeTimer = 6.0f;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "RCAI: warning in applyFleeAction: %s\n", e.what());
    }
}

void F4SEWorldSampler::dispatchReinforcements(const Action& a) {
    try {
        f4se::VirtualMachine* vm = f4se::getVM();
        if (vm) {
            vm->sendCustomEvent("RCASquadAlert", "faction_alert", a.actorId.c_str());
        }
        std::fprintf(stdout, "RCAI: squad reinforcement called for actor %s\n", a.actorId.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "RCAI: warning in dispatchReinforcements: %s\n", e.what());
    }
}

void F4SEWorldSampler::apply(const Action& a) {
    if (!diag_.initialized) return;

    try {
        f4se::PlayerCharacter* player = testPlayer_ ? testPlayer_ : f4se::getPlayer();
        f4se::TESObjectCELL* cell = (player && player->parentCell) ? player->parentCell : testCell_;
        f4se::Actor* actor = findEngineActor(cell, a.actorId);

        switch (a.type) {
            case Action::Type::MoveTo:
            case Action::Type::TakeCover:
            case Action::Type::Flank:
            case Action::Type::Retreat:
            case Action::Type::AidAlly:
                if (actor) applyUtilityAction(actor, a);
                break;
            case Action::Type::Attack:
            case Action::Type::Suppress:
                if (actor) applyCombatAction(actor, a);
                break;
            case Action::Type::Flee:
                if (actor) applyFleeAction(actor, a);
                break;
            case Action::Type::CallReinforcements:
                dispatchReinforcements(a);
                break;
            default:
                break;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "RCAI: exception in apply(): %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "RCAI: unknown exception in apply()\n");
    }
}

} // namespace rcai
