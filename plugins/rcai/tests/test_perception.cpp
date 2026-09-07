// M1 acceptance battery: perception (sight/hearing/sneak/flashlight/light),
// threat model, and player-habit memory.

#include <cstdio>

#include "../src/ai/perception.h"
#include "../src/ai/threat.h"
#include "../src/util/test.h"

using namespace rcai;

namespace {
WorldSnapshot cellWithWall() {
    WorldSnapshot w;
    w.time = 0.f;
    w.player.id = "player";
    w.player.pos = {40, 0};
    w.lightLevel = 1.f;
    w.walls.push_back({{20, -5}, {20, 5}}); // a short wall between them
    return w;
}
} // namespace

RCAI_TEST(perception_sight_in_open) {
    WorldSnapshot w = cellWithWall();
    w.walls.clear();
    w.time = 0.f;
    w.actors.push_back({});
    ActorState& a = w.actors[0];
    a.id = "e1";
    a.pos = {0, 0};
    a.facing = {1, 0}; // facing the player
    SensoryProfile p;
    p.perceptionMult = 1.f;
    for (int i = 0; i < 30; ++i) { // 1.5 s of observation at dt 0.05
        w.time += 0.05f;
        perceive(w, a, p, 0.05f);
    }
    CHECK(a.detection > 0.5f);
    CHECK_NEAR(a.lastSeenPlayer.x, 40.f, 0.1f);
}

RCAI_TEST(perception_sight_blocked_by_wall) {
    WorldSnapshot w = cellWithWall();
    w.time = 0.f;
    w.actors.push_back({});
    ActorState& a = w.actors[0];
    a.id = "e1";
    a.pos = {0, 0};
    a.facing = {1, 0};
    SensoryProfile p;
    for (int i = 0; i < 30; ++i) {
        w.time += 0.05f;
        const PerceptionResult r = perceive(w, a, p, 0.05f);
        CHECK(!r.sawPlayer);
    }
    CHECK(a.detection < 0.1f);
    // LOS blocks both sight and hearing in this model.
    CHECK(a.lastHeardTime < -100.f);
}

RCAI_TEST(perception_flashlight_extends_range) {
    // In dim light (0.2) base sight range is 45*(0.35+0.65*0.2) = 21.6 m.
    // At 28 m: invisible without flashlight, visible with it (x1.35 -> 29.2 m).
    auto sawAt = [](float dist, bool flashlight) {
        WorldSnapshot w = cellWithWall();
        w.walls.clear();
        w.lightLevel = 0.2f;
        w.playerFlashlightOn = flashlight;
        w.player.pos = {dist, 0};
        w.time = 0.f;
        w.actors.push_back({});
        ActorState& a = w.actors[0];
        a.id = "e1";
        a.pos = {0, 0};
        a.facing = {1, 0};
        SensoryProfile p;
        bool saw = false;
        for (int i = 0; i < 30; ++i) {
            w.time += 0.05f;
            saw = perceive(w, a, p, 0.05f).sawPlayer || saw;
        }
        return saw;
    };
    CHECK(!sawAt(28.f, false));
    CHECK(sawAt(28.f, true));
}

RCAI_TEST(perception_sneak_reduces_detection) {
    WorldSnapshot w = cellWithWall();
    w.walls.clear();
    w.playerNoise = 0.1f; // sneaking
    w.time = 0.f;
    w.player.pos = {30, 0}; // 30 m out
    w.actors.push_back({});
    ActorState& a = w.actors[0];
    a.id = "e1";
    a.pos = {0, 0};
    a.facing = {1, 0};
    SensoryProfile p;
    p.sightRange = 45.f;
    // At light 1.0 the full range is 45 m, but sneaking halves it to 22.5 m:
    // 30 m is now out of range.
    for (int i = 0; i < 20; ++i) {
        w.time += 0.05f;
        perceive(w, a, p, 0.05f);
    }
    CHECK(a.detection < 0.3f);
}

RCAI_TEST(perception_hearing) {
    WorldSnapshot w = cellWithWall();
    w.walls.clear();
    w.playerNoise = 0.9f; // sprinting
    w.time = 0.f;
    w.player.pos = {20, 0}; // inside 30 m hearing range at 0.9
    w.actors.push_back({});
    ActorState& a = w.actors[0];
    a.id = "e1";
    a.pos = {0, 0};
    a.facing = {0, 1}; // NOT facing the player (sight blocked by cone)
    SensoryProfile p;
    p.sightRange = 10.f; // too far to see
    for (int i = 0; i < 20; ++i) {
        w.time += 0.05f;
        perceive(w, a, p, 0.05f);
    }
    CHECK(a.suspicion > 0.5f);
    CHECK(a.lastHeardTime > 0.f);
}

RCAI_TEST(perception_detection_decays) {
    WorldSnapshot w = cellWithWall();
    w.walls.clear();
    w.time = 0.f;
    w.actors.push_back({});
    ActorState& a = w.actors[0];
    a.id = "e1";
    a.pos = {0, 0};
    a.facing = {1, 0};
    SensoryProfile p;
    for (int i = 0; i < 20; ++i) {
        w.time += 0.05f;
        perceive(w, a, p, 0.05f);
    }
    const float seen = a.detection;
    CHECK(seen > 0.5f);
    // Player moves out of sight; detection must decay.
    w.player.pos = {200, 200};
    for (int i = 0; i < 100; ++i) { // 5 s
        w.time += 0.05f;
        perceive(w, a, p, 0.05f);
    }
    CHECK(a.detection < seen - 0.1f);
}

RCAI_TEST(threat_model) {
    WorldSnapshot w;
    w.time = 1.f;
    w.actors.push_back({});
    ActorState& a = w.actors[0];
    a.id = "e1";
    updateThreat(w, a, 0.05f, true, true); // player hit
    const float after = a.threat;
    CHECK(after > 0.1f);
    updateThreat(w, a, 0.05f, true, false); // player missed
    CHECK(a.threat > after);
    for (int i = 0; i < 200; ++i) updateThreat(w, a, 0.5f, false, false); // forgetting
    CHECK(a.threat < after - 0.05f);
}

RCAI_TEST(habit_grid_camp_detection) {
    PlayerHabitGrid g;
    for (int i = 0; i < 700; ++i) { // 35 s in one cell
        g.note({5, 5}, 0.05f);
    }
    CHECK(g.concentrated(30.f, 0.6f));

    PlayerHabitGrid spread;
    for (int i = 0; i < 700; ++i)
        spread.note({float(i % 4) * 30.f - 45.f, float((i / 4) % 4) * 30.f - 45.f}, 0.05f);
    CHECK(!spread.concentrated(30.f, 0.6f));
}
