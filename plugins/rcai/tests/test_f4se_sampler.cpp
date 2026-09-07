// RCAI — Reactive Combat AI
// Unit tests for F4SEWorldSampler (M1–M7 in-game world adapter).

#include <cmath>
#include <fstream>
#include <string>

#include "../src/world/f4se_sampler.h"
#include "../src/util/test.h"

using namespace rcai;

RCAI_TEST(f4se_sampler_uninit_is_safe_noop) {
    F4SEWorldSampler sampler;
    WorldSnapshot snap;
    // Before init, sample() must return false safely (never crash)
    CHECK(!sampler.sample(snap, 0.05f));
    CHECK_EQ(sampler.diagnostics().actorsSampled, 0u);
    CHECK_EQ(sampler.diagnostics().losCallsPerFrame, 0u);

    // apply() on uninitialized sampler must be a safe no-op
    Action a;
    a.type = Action::Type::Attack;
    a.actorId = "12345";
    sampler.apply(a);
}

RCAI_TEST(f4se_sampler_loads_combat_styles) {
    F4SEWorldSampler sampler;
    const bool loaded = sampler.init("data/combat/combat_styles.json");
    CHECK(loaded);
    CHECK(sampler.diagnostics().initialized);

    // Verify decompiled CSTY mapping (integration point 1)
    // 0x001069A7 (CZ_MeleeOnly) -> sniper
    CHECK_EQ(sampler.lookupArchetype(0x001069A7, "CZ_MeleeOnly"), "sniper");
    // 0x0021634E (csCompHancockMelee) -> assault
    CHECK_EQ(sampler.lookupArchetype(0x0021634E, "csCompHancockMelee"), "assault");
    // 0x00019417 (DN109_csGunnerBossBaker) -> generic
    CHECK_EQ(sampler.lookupArchetype(0x00019417, "DN109_csGunnerBossBaker"), "generic");
    // Unknown styles fall back cleanly to generic
    CHECK_EQ(sampler.lookupArchetype(0x99999999, "NonExistent"), "generic");
    CHECK_EQ(sampler.lookupArchetype(0, ""), "generic");
}

RCAI_TEST(f4se_sampler_full_pipeline_with_mock_cell) {
    F4SEWorldSampler sampler;
    CHECK(sampler.init("data/combat/combat_styles.json"));

    // Set up mock player
    f4se::PlayerCharacter mockPlayer;
    mockPlayer.formID = 0x00000014;
    mockPlayer.formType = f4se::kFormType_ACHR;
    mockPlayer.pos = {10.0f, 20.0f, 0.0f};
    mockPlayer.rot = {0.0f, 0.0f, 1.5707963f}; // facing ~ +Y
    mockPlayer.isFlashlightOn = true;
    mockPlayer.stealthNoise = 0.45f;
    f4se::ActorValueData playerAvData[] = {{f4se::kActorValue_Health, 250.0f}};
    mockPlayer.actorValueData.entries = playerAvData;
    mockPlayer.actorValueData.count = 1;

    // Set up mock cell
    f4se::TESObjectCELL mockCell;
    mockCell.formID = 0x00012345;
    mockPlayer.parentCell = &mockCell;

    // Add a static wall occluder (FormType 36 = STAT)
    f4se::TESForm statBase;
    statBase.formType = f4se::kFormType_STAT;
    f4se::TESObjectREFR wallRefr;
    wallRefr.formID = 0x00020001;
    wallRefr.formType = f4se::kFormType_STAT;
    wallRefr.baseForm = &statBase;
    wallRefr.pos = {10.0f, 35.0f, 0.0f};
    wallRefr.rot = {0.0f, 0.0f, 0.0f};

    // Add an enemy actor (FormType 65 = ACHR)
    f4se::TESCombatStyle enemyStyle;
    enemyStyle.formID = 0x0021634E; // assault style
    f4se::TESNPC enemyNPC;
    enemyNPC.formType = f4se::kFormType_NPC_;
    enemyNPC.combatStyle = &enemyStyle;

    f4se::Actor enemyActor;
    enemyActor.formID = 0x00030001;
    enemyActor.formType = f4se::kFormType_ACHR;
    enemyActor.baseForm = &enemyNPC;
    enemyActor.pos = {10.0f, 50.0f, 0.0f};
    enemyActor.rot = {0.0f, 0.0f, 3.1415926f};
    f4se::ActorValueData enemyAvData[] = {{f4se::kActorValue_Health, 100.0f}};
    enemyActor.actorValueData.entries = enemyAvData;
    enemyActor.actorValueData.count = 1;
    enemyActor.actorFlags = f4se::Actor::kFlag_InCombat;

    // Populate cell object list
    f4se::TESObjectREFR* objects[] = {&mockPlayer, &wallRefr, &enemyActor};
    mockCell.objectList.entries = objects;
    mockCell.objectList.count = 3;

    sampler.setTestPlayer(&mockPlayer);
    sampler.setTestCell(&mockCell);

    WorldSnapshot snap;
    const bool ok = sampler.sample(snap, 0.05f);
    CHECK(ok);

    // Verify Integration Point 2: Player state
    CHECK(snap.player.isPlayer);
    CHECK_NEAR(snap.player.pos.x, 10.0f, 0.01f);
    CHECK_NEAR(snap.player.pos.z, 20.0f, 0.01f);
    CHECK_NEAR(snap.player.health, 250.0f, 0.01f);
    CHECK(snap.playerFlashlightOn);
    CHECK_NEAR(snap.playerNoise, 0.45f, 0.01f);
    CHECK_EQ(snap.playerTrail.size(), 1u);

    // Verify Integration Point 3: Occluder geometry
    CHECK_EQ(snap.walls.size(), 1u);
    CHECK_EQ(sampler.diagnostics().occludersSampled, 1u);

    // Verify Integration Point 1: Actor enumeration
    CHECK_EQ(snap.actors.size(), 1u);
    CHECK_EQ(snap.actors[0].id, std::to_string(0x00030001));
    CHECK_NEAR(snap.actors[0].pos.x, 10.0f, 0.01f);
    CHECK_NEAR(snap.actors[0].pos.z, 50.0f, 0.01f);
    CHECK_NEAR(snap.actors[0].health, 100.0f, 0.01f);
    CHECK(snap.actors[0].inCombat);
    CHECK_EQ(sampler.diagnostics().actorsSampled, 1u);

    // Verify Integration Point 4 & 5: Cover points & LOS
    CHECK(snap.coverPoints.size() > 0);
    CHECK(sampler.diagnostics().losCallsPerFrame > 0);

    // Verify Integration Point 7: apply actions to mock actor
    Action moveAct;
    moveAct.type = Action::Type::MoveTo;
    moveAct.actorId = std::to_string(0x00030001);
    moveAct.target = {15.0f, 45.0f};
    sampler.apply(moveAct);
    CHECK_NEAR(enemyActor.pos.x, 15.0f, 0.01f);
    CHECK_NEAR(enemyActor.pos.y, 45.0f, 0.01f);

    Action alertAct;
    alertAct.type = Action::Type::CallReinforcements;
    alertAct.actorId = std::to_string(0x00030001);
    sampler.apply(alertAct); // Safe dispatch to VM / log
}
