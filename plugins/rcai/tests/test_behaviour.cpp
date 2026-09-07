// M2 acceptance battery: cover solver, squad coordinator, utility behaviour,
// anti-cheese, and an end-to-end micro-scenario through HeadlessSim.

#include <cstdio>

#include "../src/ai/sim.h"
#include "../src/util/test.h"

using namespace rcai;

namespace {

WorldSnapshot combatCell() {
    WorldSnapshot w;
    w.time = 0.f;
    w.lightLevel = 1.f;
    w.factions.push_back({"assaultFaction", "assault", -1});
    w.player.id = "player";
    w.player.pos = {0, 0};
    w.player.facing = {1, 0};
    w.player.health = 500.f;
    w.player.maxHealth = 500.f;
    // A wall; a cover point in its shadow (from the player) and an open one.
    w.walls.push_back({{20, -8}, {20, 8}});
    w.coverPoints.push_back({{25, 4}, 0.8f});   // in the wall's shadow
    w.coverPoints.push_back({{60, 40}, 0.6f});  // open ground (weak cover)
    return w;
}

ActorState makeEnemy(const std::string& id, Vec2 pos) {
    ActorState a;
    a.id = id;
    a.factionIndex = 0;
    a.pos = pos;
    a.facing = normalize(Vec2{0, 0} - pos);
    a.health = a.maxHealth = 100.f;
    a.speed = 3.5f;
    a.weaponDamage = 10.f;
    a.fireInterval = 0.9f;
    a.accuracy = 0.55f;
    return a;
}

} // namespace

RCAI_TEST(cover_occlusion) {
    WorldSnapshot w = combatCell();
    // The shadow point is hidden from the player at the origin...
    CHECK(occlusionScore(w, {0, 0}, {25, 4}) > 0.5f);
    // ...the open point is (no occluder within 4 m of the sight line).
    CHECK(occlusionScore(w, {0, 0}, {60, 40}) == 0.f);
}

RCAI_TEST(cover_best_choice) {
    WorldSnapshot w = combatCell();
    ActorState a = makeEnemy("e1", {27, 12}); // visible from the player
    const CoverCandidate best = bestCover(w, a, {w.player.pos}, a.speed * 4.f);
    CHECK(best.score > 0.3f);
    CHECK(distance(best.pos, {25, 4}) < distance(best.pos, {60, 40}));
}

RCAI_TEST(squad_roles) {
    WorldSnapshot w = combatCell();
    for (int i = 0; i < 6; ++i)
        w.actors.push_back(makeEnemy("e" + std::to_string(i), {30 + i * 3.f, i * 2.f}));
    SquadCoordinator sq;
    int gi = -2;
    for (const auto& a : w.actors) gi = sq.update(w, a.id);
    CHECK_EQ(sq.groupCount(), 1u);
    const auto* g = sq.group(gi);
    CHECK(g != nullptr);
    CHECK_EQ(int(g->members.size()), 6);
    CHECK_EQ(g->roleOf(g->leader), Role::Leader);
    int suppress = 0, flank = 0, medic = 0, heavy = 0;
    for (const auto& m : g->members) {
        switch (g->roleOf(m)) {
        case Role::Suppressor: ++suppress; break;
        case Role::Flanker: ++flank; break;
        case Role::Medic: ++medic; break;
        case Role::Heavy: ++heavy; break;
        default: break;
        }
    }
    CHECK_EQ(suppress, g->suppressors);
    CHECK_EQ(flank, g->flankers);
    CHECK_EQ(medic, g->medics);
    CHECK_EQ(heavy, g->heavies);
    CHECK_EQ(suppress + flank + medic + heavy + 1, 6); // + leader
}

RCAI_TEST(squad_split_by_faction) {
    WorldSnapshot w = combatCell();
    w.factions.push_back({"other", "sniper", -1});
    w.actors.push_back(makeEnemy("e1", {30, 0}));
    ActorState s = makeEnemy("s1", {33, 0});
    s.factionIndex = 1;
    w.actors.push_back(s);
    SquadCoordinator sq;
    sq.update(w, "e1");
    const int gi = sq.update(w, "s1");
    CHECK_EQ(sq.groupCount(), 2u);
    CHECK(gi >= 0);
    CHECK_EQ(int(sq.group(gi)->members.size()), 1);
}

RCAI_TEST(squad_redistributes_on_death) {
    WorldSnapshot w = combatCell();
    w.actors.push_back(makeEnemy("e1", {30, 0}));
    w.actors.push_back(makeEnemy("e2", {33, 0}));
    w.actors.push_back(makeEnemy("e3", {36, 0}));
    SquadCoordinator sq;
    for (const auto& a : w.actors) sq.update(w, a.id);
    CHECK_EQ(int(sq.group(0)->members.size()), 3);
    // e1 dies: group must shrink next update.
    w.actors[0].health = 0.f;
    sq.update(w, "e2");
    CHECK_EQ(int(sq.group(0)->members.size()), 2);
}

RCAI_TEST(behaviour_exposed_enemy_takes_cover) {
    WorldSnapshot w = combatCell();
    ActorState e = makeEnemy("e1", {27, 12}); // exposed: sees the player
    e.inCombat = true;
    e.detection = 1.f;
    w.actors.push_back(std::move(e));
    Brain brain;
    const BrainResult r = brain.tick(w, {}, 0.05f);
    bool tookCover = false;
    for (const auto& a : r.actions)
        if (a.actorId == "e1" && a.type == Action::Type::TakeCover) tookCover = true;
    CHECK(tookCover);
}

RCAI_TEST(behaviour_low_hp_retreats) {
    WorldSnapshot w = combatCell();
    w.coverPoints.clear(); // no cover: retreat/flee should win
    ActorState e = makeEnemy("e1", {30, 10});
    e.inCombat = true;
    e.health = 10.f; // hpFrac 0.1 < fleeThreshold
    w.actors.push_back(std::move(e));
    Brain brain;
    const BrainResult r = brain.tick(w, {}, 0.05f);
    bool retreated = false;
    for (const auto& a : r.actions)
        if (a.actorId == "e1" &&
            (a.type == Action::Type::Retreat || a.type == Action::Type::Flee))
            retreated = true;
    CHECK(retreated);
}

RCAI_TEST(behaviour_camp_triggers_flank_boost) {
    WorldSnapshot w = combatCell();
    ActorState e = makeEnemy("e1", {30, 0});
    e.inCombat = true;
    e.detection = 1.f;
    w.actors.push_back(std::move(e));
    Brain brain;
    // Simulate 40 s of the player camping in one spot while shooting.
    for (int i = 0; i < 800; ++i) {
        w.time += 0.05f;
        std::vector<WorldEvent> evs;
        evs.push_back({WorldEvent::Kind::ShotFired, "player", "e1", w.player.pos, 10.f, true});
        (void)brain.tick(w, evs, 0.05f);
    }
    CHECK(brain.antiCheese().campingDetected);
    CHECK_NEAR(brain.antiCheese().flankBoost, 2.f, 1e-6);
}

RCAI_TEST(sim_micro_scenario) {
    // End-to-end: enemy detects, takes cover, shoots from cover; player
    // kills it; no crashes, sane stats.
    WorldSnapshot w = combatCell();
    w.actors.push_back(makeEnemy("e1", {30, 16})); // visible, cover within budget
    HeadlessSim sim(std::move(w), 7);
    for (int i = 0; i < 600 && sim.world().player.health > 0; ++i) {
        sim.step(0.05f);
    }
    const SimStats& s = sim.stats();
    CHECK(s.enemyShots > 0);
    CHECK(s.playerShots > 0);
    CHECK(s.enemyShotsFromCover > 0); // it used the cover point behind the wall
}
