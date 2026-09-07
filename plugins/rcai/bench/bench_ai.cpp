// M2 AI scoreboard (roadmap exit criteria).
//
// Scenario: 20 enemies across 5 archetypes in a walled cell with cover
// clusters, a scripted player who advances/strafes/auto-fires. 5 seeds x
// 120 s of simulated combat.
//
// Pass gates (docs/MODERNIZATION_ROADMAP.md, M2):
//   G1 cover-usage:  >= 30% of enemy combat time spent in cover
//   G2 flanking:     >= 2 flanking shots AND >= 2 distinct flankers
//   G3 TTK:          first kill within 30 s (600 ticks)
//   G4 sanity:       >= 40% of enemies survive past 30 s (player not
//                    one-shotting everyone -> the fight is real)
//
// Writes reports/ai_bench.txt (committed as milestone evidence).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "../src/ai/sim.h"
#include "../src/util/rng.h"

using namespace rcai;

namespace {

constexpr int kTicks = 2400;      // 120 s at 50 ms
constexpr float kDt = 0.05f;

WorldSnapshot scenario(std::uint64_t seed) {
    WorldSnapshot w;
    w.lightLevel = 1.f;
    const std::vector<std::string> archs{"assault", "sniper", "heavy", "support", "suicidal"};
    w.factions.resize(archs.size());
    for (size_t i = 0; i < archs.size(); ++i)
        w.factions[i] = {archs[i], archs[i], -1};

    w.player.id = "player";
    w.player.pos = {0, 0};
    w.player.facing = {1, 0};
    w.player.health = w.player.maxHealth = 500.f;
    w.playerNoise = 0.8f;

    Rng rng(seed);
    // Walls: two L-shaped structures.
    w.walls.push_back({{40, -30}, {40, 10}});
    w.walls.push_back({{40, 10}, {60, 10}});
    w.walls.push_back({{-10, 40}, {30, 40}});
    w.walls.push_back({{30, 40}, {30, 60}});

    // Cover clusters around each wall.
    const std::vector<Vec2> coverSpots{
        {35, -20}, {35, 0}, {50, 6}, {58, 14}, {0, 36}, {20, 44}, {26, 52},
        {-40, -20}, {-20, -40}, {-45, 20}, {70, -30}, {80, 30}, {20, -50}, {60, 55}};
    // Dense cover ring on the approach corridor (real FO4 cells are cluttered).
    for (int i = 0; i < 12; ++i) {
        const float ang = (-70.f + i * (140.f / 11.f)) * 3.14159265f / 180.f;
        const float rad = 48.f + 8.f * ((i * 37) % 5);
        w.coverPoints.push_back({Vec2{std::cos(ang) * rad, std::sin(ang) * rad}, 0.75f});
    }
    for (const auto& c : coverSpots) w.coverPoints.push_back({c, 0.75f});

    // 20 enemies in an arc 70-95 m out, grouped by archetype.
    const std::vector<std::pair<const char*, int>> plan{{"assault", 8}, {"sniper", 4},
                                                        {"heavy", 3},    {"support", 2},
                                                        {"suicidal", 3}};
    int idx = 0;
    for (const auto& [name, count] : plan) {
        const int fidx = int(std::find(archs.begin(), archs.end(), name) - archs.begin());
        for (int i = 0; i < count; ++i, ++idx) {
            ActorState a;
            a.id = std::string(name) + std::to_string(i);
            a.factionIndex = fidx;
            const float ang = (-60.f + (idx * 120.f / 20.f)) * 3.14159265f / 180.f;
            const float rad = 70.f + 25.f * rng.nextFloat();
            a.pos = {std::cos(ang) * rad, std::sin(ang) * rad};
            a.facing = normalize(Vec2{0, 0} - a.pos);
            a.speed = 3.5f;
            a.fireInterval = 0.9f;
            a.accuracy = 0.55f;
            a.weaponDamage = 10.f;
            a.health = a.maxHealth = 100.f;
            if (fidx == 1) { // sniper
                a.health = a.maxHealth = 70.f;
                a.weaponDamage = 25.f;
                a.fireInterval = 2.2f;
                a.accuracy = 0.5f;
            } else if (fidx == 2) { // heavy
                a.health = a.maxHealth = 200.f;
                a.weaponDamage = 8.f;
                a.fireInterval = 0.35f;
                a.accuracy = 0.45f;
                a.speed = 2.5f;
            } else if (fidx == 3) { // support
                a.weaponDamage = 7.f;
                a.fireInterval = 1.5f;
            } else if (fidx == 4) { // suicidal
                a.health = a.maxHealth = 60.f;
                a.weaponDamage = 12.f;
                a.accuracy = 0.4f;
                a.speed = 4.f;
            }
            w.actors.push_back(std::move(a));
        }
    }
    return w;
}

struct SeedResult {
    std::uint64_t seed;
    float coverTimeFrac = 0.f;    // combat seconds spent in cover (G1)
    float coverRatioCombat = 0.f; // shots in combat phase from cover (reported)
    float coverRatioOverall = 0.f;
    int flankingShots = 0;
    int flankers = 0;
    int firstKillTick = -1;
    float enemySurvivalAt30s = 0.f;
    int enemyShots = 0;
    int totalKills = 0;
    int heals = 0;
};

SeedResult runSeed(std::uint64_t seed, perf::FrameProfiler* prof = nullptr) {
    SeedResult r;
    r.seed = seed;
    HeadlessSim sim(scenario(seed), seed);
    sim.setProfiler(prof);

    // Snapshot stats at first contact (any enemy in combat) so the
    // combat-phase ratio is exact.
    int shotsAtContact = -1, coverAtContact = -1, aliveAt30 = -1, contactTick = -1;

    for (int t = 0; t < kTicks; ++t) {
        sim.step(kDt);
        if (contactTick < 0) {
            for (const auto& a : sim.world().actors)
                if (a.inCombat) { contactTick = t; break; }
        }
        if (t == contactTick && shotsAtContact < 0) {
            shotsAtContact = sim.stats().enemyShots;
            coverAtContact = sim.stats().enemyShotsFromCover;
        }
        if (aliveAt30 < 0 && sim.world().time >= 30.f) {
            int alive = 0;
            for (const auto& a : sim.world().actors)
                if (a.health > 0.f) ++alive;
            aliveAt30 = alive;
        }
        if (sim.world().player.health <= 0.f) break;
    }

    const SimStats& s = sim.stats();
    r.enemyShots = s.enemyShots;
    r.coverTimeFrac = s.combatTime > 0 ? float(s.coverCombatTime / s.combatTime) : 0.f;
    r.coverRatioOverall = s.enemyShots > 0 ? float(s.enemyShotsFromCover) / s.enemyShots : 0.f;
    // Exact combat-phase ratio: total minus the contact-tick snapshot.
    const int combatShots = s.enemyShots - std::max(0, shotsAtContact);
    const int combatCover = s.enemyShotsFromCover - std::max(0, coverAtContact);
    r.coverRatioCombat = combatShots > 0 ? float(combatCover) / combatShots : r.coverRatioOverall;
    r.flankingShots = s.enemyShotsFlanking;
    r.flankers = int(s.flankers.size());
    r.firstKillTick = s.firstKillTick;
    r.enemySurvivalAt30s = aliveAt30 > 0 ? float(aliveAt30) / float(sim.world().actors.size()) : 0.f;
    r.totalKills = s.totalKills;
    r.heals = s.heals;
    return r;
}

} // namespace

int main(int argc, char** argv) {
    const int kSeeds = 5;
    perf::FrameProfiler profiler;
    std::string profilePath;
    for (int i = 1; i + 1 < argc; ++i)
        if (argv[i] == std::string("--profile")) profilePath = argv[++i];
    if (!profilePath.empty()) {
        profiler.setSubsystems({"decision", "sim_step"});
    }
    std::vector<SeedResult> results;
    for (int i = 0; i < kSeeds; ++i)
        results.push_back(runSeed(1000 + i, profilePath.empty() ? nullptr : &profiler));
    if (!profilePath.empty()) {
        profiler.exportCsv(profilePath);
        std::printf("profiler CSV -> %s (%d frames)\n", profilePath.c_str(),
                    profiler.summary().frames);
    }

    auto avg = [&](float (SeedResult::*f)) {
        float t = 0;
        for (const auto& r : results) t += r.*f;
        return t / kSeeds;
    };
    auto minOf = [&](int (SeedResult::*f)) {
        int m = 1 << 30;
        for (const auto& r : results) m = std::min(m, r.*f);
        return m;
    };

    const float coverTime = avg(&SeedResult::coverTimeFrac);
    const float coverCombat = avg(&SeedResult::coverRatioCombat);
    const float coverOverall = avg(&SeedResult::coverRatioOverall);
    const int flanks = minOf(&SeedResult::flankingShots);
    const int flankers = minOf(&SeedResult::flankers);
    const int ttk = minOf(&SeedResult::firstKillTick);
    const float survival = avg(&SeedResult::enemySurvivalAt30s);

    std::ostringstream os;
    os << "RCAI AI scoreboard (M2) — " << kSeeds << " seeds x 120 s\n\n";
    os << "seed    coverTime  coverShots  flankingShots  flankers  firstKillTick  survival@30s  kills\n";
    for (const auto& r : results)
        os << r.seed << "  " << std::fixed << std::setprecision(3) << r.coverTimeFrac << "       "
           << r.coverRatioCombat << "        " << r.flankingShots << "          " << r.flankers
           << "       " << r.firstKillTick << "             " << r.enemySurvivalAt30s << "       "
           << r.totalKills << "\n";

    os << std::fixed << std::setprecision(3) << "\navg     " << coverTime << "       "
       << coverCombat << "        " << flanks << "          " << flankers << "       " << ttk
       << "             " << survival << "\n\n";

    struct Gate {
        const char* name;
        bool pass;
        std::string detail;
    } gates[] = {
        // Honest cover metric: share of enemy combat time spent in cover.
        // Stock FO4 behaviour is ~15% (bPartialCover); 50% is the in-game
        // stretch target (full 3D cover geometry, no suicidal archetypes).
        {"G1 combat time in cover >= 30%", coverTime >= 0.30f,
         "cover time fraction " + std::to_string(coverTime) +
         ", shots from cover " + std::to_string(coverOverall)},
        {"G2 flanking >= 2 shots and >= 2 distinct flankers",
         flanks >= 2 && flankers >= 2,
         std::to_string(flanks) + " flanking shots, " + std::to_string(flankers) + " flankers"},
        {"G3 first kill within 30 s (600 ticks)", ttk >= 0 && ttk <= 600,
         "first kill tick " + std::to_string(ttk)},
        {"G4 >= 40% enemy survival at 30 s (fight is real)", survival >= 0.40f,
         "survival " + std::to_string(survival)},
    };

    int failed = 0;
    for (const auto& g : gates) {
        if (!g.pass) ++failed;
        os << (g.pass ? "PASS " : "FAIL ") << g.name << "   [" << g.detail << "]\n";
    }
    os << "\n" << (failed == 0 ? "ALL GATES PASSED" : "GATES FAILED") << "\n";

    // Save the report.
    {
        std::ofstream out("reports/ai_bench.txt");
        if (out) out << os.str();
    }
    std::printf("%s", os.str().c_str());
    return failed == 0 ? 0 : 1;
}
