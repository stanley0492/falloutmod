// M3 acceptance battery: profiler CSV, INI tuner, crash watchdog, faction
// memory persistence.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../src/perf/ini_tuner.h"
#include "../src/perf/profiler.h"
#include "../src/perf/watchdog.h"
#include "../src/worldsim/faction_memory.h"
#include "../src/util/test.h"

using namespace rcai;

RCAI_TEST(profiler_csv_and_summary) {
    perf::FrameProfiler p;
    p.setSubsystems({"brain", "streaming"});
    for (int i = 0; i < 100; ++i) {
        p.beginFrame();
        p.record("brain", 1.0 + (i % 5) * 0.5);
        if (i % 10 == 0) p.record("streaming", 8.0);
        p.endFrame(16.0);
    }
    const perf::FrameProfiler::Summary s = p.summary();
    CHECK_EQ(s.frames, 100);
    CHECK_NEAR(s.medianMs, 16.0, 0.01);
    const std::string path = "test_profiler.csv";
    p.exportCsv(path);
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    CHECK(line.find("brain") != std::string::npos);
    int rows = 1;
    while (std::getline(in, line)) ++rows;
    CHECK_EQ(rows, 101); // header + 100 frames
    std::remove(path.c_str());
}

RCAI_TEST(profiler_scopes) {
    perf::FrameProfiler p;
    p.setSubsystems({"work"});
    p.beginFrame();
    {
        perf::FrameProfiler::Scope sc(&p, "work");
        volatile double x = 0;
        for (int i = 0; i < 100000; ++i) x += 1.0;
        (void)x;
    }
    p.endFrame();
    const perf::FrameProfiler::Summary s = p.summary();
    CHECK_EQ(s.frames, 1);
    CHECK(s.medianMs >= 0.0);
}

RCAI_TEST(tuner_writes_baseline_and_is_idempotent) {
    const std::string path = "test_tuner.ini";
    {
        std::ofstream out(path);
        out << "[Papyrus]\nfUpdateBudgetMS = 1.0000\n[General]\nuGridsToLoad = 5\n";
    }
    perf::TunerSettings ts;
    perf::TunerReport r1 = perf::IniTuner::tune(path, ts);
    CHECK(r1.changes.size() >= 2);
    const IniFile after = IniFile::load(path);
    CHECK_NEAR(after.getFloat("Papyrus", "fUpdateBudgetMS"), 2.0f, 1e-3f);
    CHECK_NEAR(after.getFloat("HAVOK", "fMaxTime"), 0.016f, 1e-4f);
    // Second run: no changes (idempotent).
    perf::TunerReport r2 = perf::IniTuner::tune(path, ts);
    CHECK_EQ(r2.changes.size(), 0u);
    std::remove(path.c_str());
}

RCAI_TEST(crash_watchdog_dump_and_restore) {
    const std::string dir = "test_crash_dir";
    std::filesystem::create_directories(dir);
    perf::CrashWatchdog wd(dir);
    const std::string path = wd.writeDump("test-exception", 1234, 512.5, 37, 12, "Goodneighbor");
    CHECK_EQ(path, std::string("test_crash_dir/rcai_crash_0.json"));
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    const json::Value v = json::Value::parse(ss.str());
    CHECK_EQ(v.find("what")->asString(), std::string("test-exception"));
    CHECK_EQ(v.find("pid")->asInt(), 1234);
    CHECK_EQ(v.find("last_cell")->asString(), std::string("Goodneighbor"));
    in.close();
    std::remove(path.c_str());

    // Restore decision: only restores once per pid.
    long restored = 0;
    const std::string pidFile = "test_restore_pid.txt";
    CHECK(!perf::CrashWatchdog::shouldRestore(999, pidFile, restored));
    perf::CrashWatchdog::markRestored(pidFile, 999);
    CHECK(perf::CrashWatchdog::shouldRestore(999, pidFile, restored));
    CHECK(!perf::CrashWatchdog::shouldRestore(1000, pidFile, restored));
    std::remove(pidFile.c_str());
    std::filesystem::remove_all(dir);
}

RCAI_TEST(faction_memory_lifecycle) {
    worldsim::FactionMemory fm;
    fm.record("Goodneighbor", -1.f, 10.f, "murder");
    fm.record("Goodneighbor", -1.f, 11.f, "murder");
    CHECK(fm.hostility("Goodneighbor") > 0.5f);
    fm.record("DiamondCity", +1.f, 12.f, "trade");
    fm.record("DiamondCity", +1.f, 13.f, "trade");
    CHECK(fm.hostility("DiamondCity") < -0.5f);

    // Persistence roundtrip.
    const std::string text = fm.toJson().dump();
    const worldsim::FactionMemory back = worldsim::FactionMemory::fromJson(text);
    CHECK_EQ(int(back.size()), 2);
    CHECK_NEAR(back.hostility("Goodneighbor"), fm.hostility("Goodneighbor"), 1e-4f);

    // Forgetting pulls attitude toward neutral.
    for (int i = 0; i < 200; ++i) fm.decay(0.9f);
    CHECK(std::fabs(fm.hostility("Goodneighbor")) < 0.5f);
}

RCAI_TEST(faction_memory_seed) {
    // Seed loader over the tools/worldsim.py schema.
    const std::string seed =
        "{\"factions\": [\n"
        "  {\"edid\": \"RAIDERS\", \"hostility_class\": \"Hostile\"},\n"
        "  {\"edid\": \"INSTITUTE\", \"hostility_class\": \"Ally\"},\n"
        "  {\"edid\": \"GOODNEIGHBOR\", \"hostility_class\": \"Friendly\"},\n"
        "  {\"edid\": \"YAOGUAI\", \"hostility_class\": \"Neutral\"} ]}";
    const worldsim::FactionMemory fm = worldsim::FactionMemory::loadSeed(seed);
    CHECK_EQ(int(fm.size()), 4);
    CHECK(fm.hostility("faction:RAIDERS") > 0.7f);
    CHECK(fm.hostility("faction:INSTITUTE") < -0.6f);
    CHECK_NEAR(fm.hostility("faction:YAOGUAI"), 0.f, 1e-6f);
    // Corrupt input: no throw, empty memory.
    const worldsim::FactionMemory bad = worldsim::FactionMemory::loadSeed("not json");
    CHECK_EQ(int(bad.size()), 0);
}
