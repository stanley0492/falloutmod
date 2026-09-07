#pragma once
// RCAI — Reactive Combat AI
// F4SE World Adapter: bridges the platform-independent Brain to Fallout 4 engine.
//
// Adheres strictly to the 7 integration points in docs/INTEGRATION_CHECKLIST.md:
//   1. Actor enumeration -> ActorState (3D->2D x/z projection, health, faction, CSTY archetypes)
//   2. Player state (pos, facing, noise, flashlight, playerTrail)
//   3. Occluder geometry -> 2D wall segments from statics/land
//   4. Cover points -> candidate generation & occlusion scoring vs player + trail
//   5. LOS implementation -> geometric segment raycast + engine hook
//   6. Perception feeding -> environment metrics into snapshot (brain handles GMST logic)
//   7. apply() -> engine package mapping (AIUtilityPackage, AICombatPackage, AIFleePackage, Papyrus)
//
// Safety: safe no-op if engine state is absent; try/catch & defensive null checks;
// never CTD under any condition.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "world.h"
#include "f4se_types.h"

namespace rcai {

struct SamplerDiagnostics {
    std::size_t actorsSampled = 0;
    std::size_t losCallsPerFrame = 0;
    std::size_t coverPointsPerCell = 0;
    std::size_t occludersSampled = 0;
    bool initialized = false;
    bool inGameBound = false;
};

class F4SEWorldSampler : public IWorldSampler {
public:
    F4SEWorldSampler();
    ~F4SEWorldSampler() override;

    // Load combat style mappings from Data/RCAI/combat/combat_styles.json
    bool init(const std::string& combatStylesPath);

    // IWorldSampler seam
    bool sample(WorldSnapshot& out, float dt) override;
    void apply(const Action& a) override;

    // Diagnostics & status
    const SamplerDiagnostics& diagnostics() const { return diag_; }
    void resetFrameStats();

    // Archetype resolution helper (edid or hex formid -> archetype string)
    std::string lookupArchetype(std::uint32_t formId, const std::string& edid) const;
    std::size_t loadedStylesCount() const { return styleEdidToArchetype_.size(); }

    // Headless test harness support
    void setTestCell(f4se::TESObjectCELL* cell) { testCell_ = cell; }
    void setTestPlayer(f4se::PlayerCharacter* player) { testPlayer_ = player; }

private:
    SamplerDiagnostics diag_;
    std::unordered_map<std::uint32_t, std::string> styleFormIdToArchetype_;
    std::unordered_map<std::string, std::string> styleEdidToArchetype_;

    f4se::TESObjectCELL* testCell_ = nullptr;
    f4se::PlayerCharacter* testPlayer_ = nullptr;

    // Integration Point 1: Actor enumeration
    bool sampleActors(f4se::TESObjectCELL* cell, f4se::PlayerCharacter* player, WorldSnapshot& out);

    // Integration Point 2: Player state
    bool samplePlayer(f4se::PlayerCharacter* player, WorldSnapshot& out, float dt);

    // Integration Point 3: Occluder geometry -> 2D wall segments
    bool sampleOccluders(f4se::TESObjectCELL* cell, const Vec2& playerPos, WorldSnapshot& out);

    // Integration Point 4: Cover points generation & scoring
    bool generateCoverPoints(WorldSnapshot& out);

    // Integration Point 5: LOS test
    bool checkLOS(const Vec2& from, const Vec2& to, const WorldSnapshot& snapshot);

    // Integration Point 7: Action dispatch helpers
    void applyUtilityAction(f4se::Actor* actor, const Action& a);
    void applyCombatAction(f4se::Actor* actor, const Action& a);
    void applyFleeAction(f4se::Actor* actor, const Action& a);
    void dispatchReinforcements(const Action& a);

    // Helper to find live actor reference by string ID
    f4se::Actor* findEngineActor(f4se::TESObjectCELL* cell, const std::string& id);
};

} // namespace rcai
