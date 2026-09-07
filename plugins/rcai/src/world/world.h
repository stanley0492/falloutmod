#pragma once
// The platform-independent world model the RCAI brain operates on.
//
// In-game, an F4SE WorldSampler (docs/INTEGRATION_CHECKLIST.md) fills this
// structure each tick from the engine (actor positions, LOS, factions,
// inventory). In tests/benchmarks, the sim backend constructs it directly.
// Keeping this seam is what lets the entire AI brain run headlessly.

#include <ostream>
#include <string>
#include <vector>

#include "../util/math.h"

namespace rcai {

// ---- factions -------------------------------------------------------------

struct FactionProfile {
    std::string id;
    // Combat-style archetypes (see ai/combat_styles.h for the generated sets):
    // "assault" | "sniper" | "heavy" | "support" | "suicidal" | "generic"
    std::string archetype = "generic";
    int relation = 0; // 0 neutral, +1 ally/friend, -1 enemy
};

// ---- actors ----------------------------------------------------------------

enum class Role : int {
    None = 0,
    Suppressor,
    Flanker,
    Medic,
    Heavy,
    Leader,
};

struct ActorState {
    std::string id;
    int factionIndex = 0;
    Vec2 pos{0, 0};
    Vec2 facing{1, 0};
    float health = 100.f;
    float maxHealth = 100.f;
    float speed = 3.f;            // units/sec
    float weaponDamage = 10.f;    // avg per shot
    float fireInterval = 1.2f;    // seconds between shots (weapon)
    float accuracy = 0.6f;        // base hit probability at 10 m
    float detection = 0.f;        // 0..1 accumulated sight evidence
    float suspicion = 0.f;        // slower-decaying curiosity meter
    float threat = 0.f;           // threat this actor poses to the player (0..1)
    bool inCombat = false;
    Role role = Role::None;
    std::string package = "Idle"; // current engine package tag (for logging)
    float ammo = 30.f;
    float reloadTimer = 0.f;
    float lastShotTime = -999.f;
    Vec2 lastSeenPlayer{0, 0};
    float lastSeenTime = -999.f;
    float lastHeardTime = -999.f;
    Vec2 moveTarget{0, 0};
    bool moving = false;
    bool enRouteToCover = false; // committed to a cover point; keeps the goal alive
    float enRouteTime = 0.f;     // abandoned after 6 s if unreachable
    Vec2 coverPos{0, 0};
    bool hasCover = false;
    float coverQuality = 0.f;
    float peekTimer = 0.f;        // seconds until next peek-shoot window
    bool isPlayer = false;
    int kills = 0;
};

inline std::ostream& operator<<(std::ostream& os, Role r) {
    static const char* names[] = {"None", "Suppressor", "Flanker", "Medic", "Heavy", "Leader"};
    return os << names[int(r) < 6 ? int(r) : 0];
}

// ---- static cell geometry (precomputed by the sampler) ----------------------

struct WallSegment {
    Vec2 a{0, 0}, b{0, 0};
};

struct CoverPoint {
    Vec2 pos{0, 0};
    float quality = 0.5f; // intrinsic quality (low = thin cover)
};

// ---- world events (input to the brain) --------------------------------------

struct WorldEvent {
    enum class Kind {
        ShotFired,      // shooterId fired at targetId, hit?
        ActorDied,
        PlayerMoved,
        NoiseMade,      // at pos, loudness 0..1
        CellChanged,
    } kind;
    std::string actorId;
    std::string otherId;
    Vec2 pos{0, 0};
    float value = 0.f;
    bool hit = false;
};

// ---- snapshot ----------------------------------------------------------------

struct WorldSnapshot {
    float time = 0.f;
    int cellIndex = 0;
    float lightLevel = 1.f; // 0..1, drives sight range (time of day / flashlight)
    ActorState player;
    std::vector<ActorState> actors;
    std::vector<WallSegment> walls;
    std::vector<CoverPoint> coverPoints;
    std::vector<FactionProfile> factions;
    bool playerFlashlightOn = false;
    float playerNoise = 0.f; // 0..1 current movement noise
    // Recent player positions (the sampler keeps a short trail). Cover is
    // scored against these too: enemies use *memory* of where the player
    // has been, not just where they stand (real tactical behaviour).
    std::vector<Vec2> playerTrail;

    WorldSnapshot() { player.isPlayer = true; }

    bool hasLOS(Vec2 a, Vec2 b) const {
        for (const auto& w : walls)
            if (segmentsIntersect(a, b, w.a, w.b)) return false;
        return true;
    }

    ActorState* findActor(const std::string& id) {
        if (player.id == id) return &player;
        for (auto& a : actors)
            if (a.id == id) return &a;
        return nullptr;
    }

    const ActorState* findActor(const std::string& id) const {
        if (player.id == id) return &player;
        for (const auto& a : actors)
            if (a.id == id) return &a;
        return nullptr;
    }
};

// ---- actions (output of the brain) -------------------------------------------

struct Action {
    enum class Type {
        None,
        MoveTo,
        TakeCover,
        Attack,       // direct fire on a target
        Suppress,     // fire to pin (no aim for kill)
        Flank,        // move to a flanking position
        Retreat,      // fall back to rally point
        Flee,         // disengage entirely
        AidAlly,      // heal/support a teammate
        CallReinforcements,
        UseVehicle,
        Search,       // investigate last-seen position
        Patrol,
        Reload,
        Idle,
    } type = Type::None;
    std::string actorId;
    Vec2 target{0, 0};
    std::string targetActorId;
    float param = 0.f; // role-specific (e.g. urgency 0..1)

    static const char* typeName(Type t) {
        switch (t) {
        case Type::MoveTo: return "MoveTo";
        case Type::TakeCover: return "TakeCover";
        case Type::Attack: return "Attack";
        case Type::Suppress: return "Suppress";
        case Type::Flank: return "Flank";
        case Type::Retreat: return "Retreat";
        case Type::Flee: return "Flee";
        case Type::AidAlly: return "AidAlly";
        case Type::CallReinforcements: return "CallReinforcements";
        case Type::UseVehicle: return "UseVehicle";
        case Type::Search: return "Search";
        case Type::Patrol: return "Patrol";
        case Type::Reload: return "Reload";
        case Type::Idle: return "Idle";
        default: return "None";
        }
    }
};

// A brain tick: one snapshot in, a set of actions + diagnostics out.
struct BrainResult {
    std::vector<Action> actions;
    std::string debugLine; // single-line state for logs / debug overlay
};

// ---------------------------------------------------------------------------
// world sampler seam
// ---------------------------------------------------------------------------

class IWorldSampler {
public:
    virtual ~IWorldSampler() = default;
    // Fills the snapshot; returns false when no world data is available.
    virtual bool sample(WorldSnapshot& out, float dt) = 0;
    // Applies one brain action to the engine.
    virtual void apply(const Action& a) = 0;
};

} // namespace rcai
