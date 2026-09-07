/*
 * RCAI — Reactive Combat AI
 * =========================
 * F4SE plugin · FO4 Modernization Program · Milestones M1–M3 adapter.
 *
 * Architecture:
 *
 *   ┌────────────┐   sample    ┌──────────────────────────────┐
 *   │ WorldSampler│───────────▶│  Brain (platform-independent) │
 *   │ (F4SE, see  │            │  perception · threat · memory │
 *   │ INTEGRATION │            │  cover · squad · utility AI   │
 *   │ _CHECKLIST) │            │  anti-cheese · adaptive diff  │
 *   └────────────┘  ◀───────── └──────────────────────────────┘
 *        apply Actions            ▲
 *                                 │ tables (data/combat, data/worldsim)
 *   Config: RCAI.ini (same dir)  │
 *   Perf:   FrameProfiler / IniTuner / CrashWatchdog
 *   World:  FactionMemory (JSON-persisted settlement memory)
 *
 * The Brain runs headlessly in CI (plugins/rcai/tests, bench) — this file is
 * the thin game-facing adapter. Until the WorldSampler integration points in
 * docs/INTEGRATION_CHECKLIST.md are wired on a Windows machine, the plugin is
 * a safe no-op in-game (console commands, tuning and crash dumps still work).
 */

#include <f4se/PluginAPI.h>
#include <f4se/PluginManager.h>
#include <f4se/PluginUtilities.h>
#include <f4se/ConsoleUtil.h>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <string>

#include "util/ini.h"
#include "world/world.h"
#include "ai/behaviour.h"
#include "perf/profiler.h"
#include "perf/ini_tuner.h"
#include "perf/watchdog.h"
#include "worldsim/faction_memory.h"

namespace {

using namespace rcai;

constexpr const char* kPluginName = "RCAI";
constexpr const char* kPluginVersion = "0.2.0";
constexpr const wchar_t* kIniFile = L"RCAI.ini";
constexpr const wchar_t* kIniSection = L"RCAI";
constexpr std::uint32_t kDebugKey = 0x32; // VK_F3

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

struct RCAIConfig {
    bool bEnabled = true;
    std::int32_t iUpdateIntervalMS = 100;
    float fPerceptionMult = 1.0f;
    bool bDebugLogging = false;
    // [Perf]
    float fPapyrusBudgetMS = 2.0f;
    float fHavokMaxTime = 0.016f;
    // [WorldSim]
    bool bFactionMemory = true;
};

RCAIConfig g_config;

std::wstring GetPluginDir() {
    HMODULE hMod = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"RCAI", &hMod)) {
        wchar_t path[MAX_PATH]{};
        if (GetModuleFileNameW(hMod, path, MAX_PATH) > 0) {
            std::wstring s(path);
            const size_t pos = s.find_last_of(L"\\/");
            if (pos != std::wstring::npos) return s.substr(0, pos + 1);
        }
    }
    return L".";
}

bool LoadConfig() {
    const std::wstring dir = GetPluginDir();
    const std::wstring iniPath(dir + kIniFile);

    g_config.bEnabled =
        GetPrivateProfileIntW(kIniSection, L"bEnabled", 1, iniPath.c_str()) != 0;
    g_config.iUpdateIntervalMS =
        GetPrivateProfileIntW(kIniSection, L"iUpdateIntervalMS", 100, iniPath.c_str());
    g_config.fPerceptionMult =
        GetPrivateProfileFloatW(kIniSection, L"fPerceptionMult", 1.0f, iniPath.c_str());
    g_config.bDebugLogging =
        GetPrivateProfileIntW(kIniSection, L"bDebugLogging", 0, iniPath.c_str()) != 0;
    g_config.fPapyrusBudgetMS =
        GetPrivateProfileFloatW(L"Perf", L"fPapyrusBudgetMS", 2.0f, iniPath.c_str());
    g_config.fHavokMaxTime =
        GetPrivateProfileFloatW(L"Perf", L"fHavokMaxTime", 0.016f, iniPath.c_str());
    g_config.bFactionMemory =
        GetPrivateProfileIntW(L"WorldSim", L"bFactionMemory", 1, iniPath.c_str()) != 0;

    F4SE::LogInfo("%s: loaded config from %ls", kPluginName, iniPath.c_str());
    return true;
}

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

// The in-game sampler. Each numbered item is a documented integration point
// (docs/INTEGRATION_CHECKLIST.md) — an F4SE API call list that runs on
// Windows. Until wired, sample() returns false and the plugin no-ops safely.
class F4SEWorldSampler : public IWorldSampler {
public:
    bool sample(WorldSnapshot& /*out*/, float /*dt*/) override {
        return false; // INTEGRATION POINT 1..6 pending (see checklist)
    }
    void apply(const Action& /*a*/) override {
        // INTEGRATION POINT 7: translate Action -> engine package/steer calls.
    }
};

// ---------------------------------------------------------------------------
// runtime state
// ---------------------------------------------------------------------------

Brain g_brain;
F4SEWorldSampler g_sampler;
worldsim::FactionMemory g_factionMemory;
perf::FrameProfiler g_profiler;
perf::CrashWatchdog g_watchdog;
std::atomic<bool> g_active{true};
std::atomic<std::uint64_t> g_tickCount{0};
std::uint64_t g_lastTickMS = 0;
std::string g_f4seVersion;

void Tick(const std::uint64_t now) {
    g_tickCount.fetch_add(1, std::memory_order_relaxed);

    g_profiler.beginFrame();
    const float dt = (g_config.iUpdateIntervalMS > 0)
                         ? g_config.iUpdateIntervalMS / 1000.f
                         : 0.016f;
    WorldSnapshot world;
    if (g_sampler.sample(world, dt)) {
        const BrainResult result = g_brain.tick(world, {}, dt);
        for (const auto& a : result.actions) g_sampler.apply(a);
        g_profiler.record("brain", 0.0); // in-game: real scope timing
    }
    g_profiler.endFrame();

    if (g_config.bFactionMemory) g_factionMemory.decay(1.0f - 0.0001f);

    const std::uint64_t n = g_tickCount.load(std::memory_order_relaxed);
    if (g_config.bDebugLogging && (n % 1000 == 0))
        F4SE::LogInfo("%s: tick %llu | %s", kPluginName, (unsigned long long)n,
                      g_brain.debug(world).c_str());
    (void)now;
}

void UpdateHandler(const F4SE::MessagingInterface::Message* msg) {
    if (!msg || msg->type != F4SE::MessagingInterface::kMessage_Update) return;
    if (!g_config.bEnabled || !g_active.load(std::memory_order_acquire)) return;
    const std::uint64_t now = GetTickCount64();
    if (g_config.iUpdateIntervalMS > 0 &&
        now - g_lastTickMS < (std::uint64_t)g_config.iUpdateIntervalMS)
        return;
    g_lastTickMS = now;
    Tick(now);
}

void InputHandler(const F4SE::InputInterface::KeyData* keyData) {
    if (!keyData || !keyData->isDown) return;
    if (keyData->keyCode == kDebugKey)
        F4SE::LogInfo("%s: F3 pressed (debug overlay hook — see INTEGRATION_CHECKLIST §8)",
                      kPluginName);
}

std::string wstringToString(const std::wstring& w) {
    if (w.empty()) return "";
    std::string s(w.size(), '\0');
    for (size_t i = 0; i < w.size(); ++i) s[i] = static_cast<char>(w[i] & 0x7f);
    return s;
}

// ---------------------------------------------------------------------------
// console commands (F4SE 0.7.x std::string callback form — no dangling
// buffers: the VM owns the returned string)
// ---------------------------------------------------------------------------

std::string CmdRCAIStatus(const F4SE::ConsoleCommand::Args& args) {
    (void)args;
    const perf::FrameProfiler::Summary s = g_profiler.summary();
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "RCAI v%s (F4SE %s) | active=%s ticks=%llu | frames=%d med=%.1fms p95=%.1fms | "
                  "mem=%zu settlements | %s",
                  kPluginVersion, g_f4seVersion.c_str(),
                  g_active.load(std::memory_order_acquire) ? "on" : "off",
                  (unsigned long long)g_tickCount.load(std::memory_order_relaxed), s.frames,
                  s.medianMs, s.p95Ms, g_factionMemory.size(),
                  g_brain.antiCheese().describe().c_str());
    F4SE::LogInfo("%s: %s", kPluginName, buf);
    return buf;
}

std::string CmdRCAIToggle(const F4SE::ConsoleCommand::Args& args) {
    (void)args;
    const bool now = !g_active.load(std::memory_order_acquire);
    g_active.store(now, std::memory_order_release);
    F4SE::LogInfo("%s: tick processing %s", kPluginName, now ? "enabled" : "disabled");
    return now ? "RCAI: tick processing enabled" : "RCAI: tick processing disabled";
}

std::string CmdRCAITune(const F4SE::ConsoleCommand::Args& args) {
    (void)args;
    // Applies the performance INI baseline to Fallout4Custom.ini next to the
    // game exe (see data/engine INI map for the section names).
    const std::wstring dir = GetPluginDir();
    const std::wstring iniPath(dir + L"..\\..\\Fallout4Custom.ini");
    perf::TunerSettings ts;
    ts.papyrusUpdateBudgetMs = g_config.fPapyrusBudgetMS;
    ts.havokMaxTime = g_config.fHavokMaxTime;
    const perf::TunerReport rep = perf::IniTuner::tune(wstringToString(iniPath), ts);
    for (const auto& c : rep.changes) F4SE::LogInfo("%s: ini: %s", kPluginName, c.c_str());
    return "RCAI: tuned " + std::to_string(rep.changes.size()) + " INI keys (see f4se.log)";
}

std::string CmdRCAIDump(const F4SE::ConsoleCommand::Args& args) {
    (void)args;
    const std::string path =
        g_watchdog.writeDump("manual", GetCurrentProcessId(), 0.0, 0, 0, "rcai_dump");
    F4SE::LogInfo("%s: crash dump test -> %s", kPluginName, path.c_str());
    return path.empty() ? "RCAI: dump failed" : "RCAI: dump written: " + path;
}

std::string CmdRCAIDumpProf(const F4SE::ConsoleCommand::Args& args) {
    if (args.argv.size() < 2)
        return "RCAI: usage: rcai_dump_prof <path.csv>";
    const std::string path = args.argv[1];
    g_profiler.exportCsv(path);
    F4SE::LogInfo("%s: profiler CSV -> %s", kPluginName, path.c_str());
    return "RCAI: profiler CSV written: " + path;
}

std::string CmdRCAIDumpMemory(const F4SE::ConsoleCommand::Args& args) {
    (void)args;
    const std::string dir = wstringToString(GetPluginDir());
    const std::string path = dir + "..\\RCAI\\memory_dump.json";
    std::ofstream out(path, std::ios::binary);
    if (!out) return "RCAI: memory dump failed (write access?)";
    out << g_factionMemory.toJson().dump(2);
    F4SE::LogInfo("%s: faction memory -> %s", kPluginName, path.c_str());
    return "RCAI: faction memory dumped: " + path + " (" +
           std::to_string(g_factionMemory.size()) + " entries)";
}

std::string CmdRCAISandboxMode(const F4SE::ConsoleCommand::Args& args) {
    if (args.argv.size() < 2)
        return "RCAI: usage: rcai_sandbox_mode <0|1|2|3>";
    const long mode = std::strtol(args.argv[1].c_str(), nullptr, 10);
    if (mode < 0 || mode > 3) return "RCAI: mode must be 0..3";
    const std::wstring iniPath(GetPluginDir() + kIniFile);
    WritePrivateProfileIntW(kIniSection, L"iRCAISandboxMode", mode, iniPath.c_str());
    F4SE::LogInfo("%s: sandbox mode -> %ld", kPluginName, mode);
    return "RCAI: sandbox mode " + std::to_string(mode) + " (applied next config poll)";
}

std::string CmdRCAIInjectRaid(const F4SE::ConsoleCommand::Args& args) {
    if (args.argv.size() < 2)
        return "RCAI: usage: rcai_inject_raid <settlement>";
    const std::string& settlement = args.argv[1];
    g_factionMemory.record(settlement, -1.0f, 0.0f, "raid (sandbox injection)");
    F4SE::LogInfo("%s: raid injected at %s", kPluginName, settlement.c_str());
    return "RCAI: raid injected at " + settlement;
}

// ---------------------------------------------------------------------------
// plugin callback
// ---------------------------------------------------------------------------

class RCAIPlugin : public F4SE::IPluginCallback {
public:
    virtual bool Init(const char* version, const char* calculated) override {
        (void)calculated;
        using namespace F4SE;

        g_f4seVersion = version ? version : "?";
        if (!LoadConfig())
            LogError("%s: could not read RCAI.ini, using defaults", kPluginName);

        LogInfo("%s v%s: initializing (F4SE version %s)", kPluginName, kPluginVersion,
                g_f4seVersion.c_str());

        g_profiler.setSubsystems({"brain", "perception", "behaviour", "streaming", "papyrus"});

        ConsoleCommand::RegisterCommand("rcai_status",
                                        "RCAI: show version, state, perf and AI stats",
                                        &CmdRCAIStatus);
        ConsoleCommand::RegisterCommand("rcai_toggle",
                                        "RCAI: enable/disable tick processing", &CmdRCAIToggle);
        ConsoleCommand::RegisterCommand("rcai_tune",
                                        "RCAI: apply performance INI baseline", &CmdRCAITune);
        ConsoleCommand::RegisterCommand("rcai_dump",
                                        "RCAI: write a test crash dump", &CmdRCAIDump);
        ConsoleCommand::RegisterCommand("rcai_dump_prof",
                                        "RCAI: export profiler CSV (tools/perf_report.py input)",
                                        &CmdRCAIDumpProf);
        ConsoleCommand::RegisterCommand("rcai_dump_memory",
                                        "RCAI: dump faction-memory ledger to Data/RCAI/",
                                        &CmdRCAIDumpMemory);
        ConsoleCommand::RegisterCommand("rcai_sandbox_mode",
                                        "RCAI: set sandbox faction mode 0..3", &CmdRCAISandboxMode);
        ConsoleCommand::RegisterCommand("rcai_inject_raid",
                                        "RCAI: inject a raid event at a settlement",
                                        &CmdRCAIInjectRaid);

        GetMessaging().Register("f4se::update", &UpdateHandler);
        GetInput().RegisterListener(kDebugKey, &InputHandler);

        LogInfo("%s v%s: initialized (world sampler: %s)", kPluginName, kPluginVersion,
                "stub — see docs/INTEGRATION_CHECKLIST.md");
        return true;
    }

    virtual void Shutdown(void) override {
        const std::string dir = wstringToString(GetPluginDir());
        if (g_config.bFactionMemory && g_factionMemory.size() > 0) {
            std::ofstream out(dir + "RCAI_faction_memory.json", std::ios::binary);
            if (out) out << g_factionMemory.toJson().dump(2);
        }
        F4SE::LogInfo("%s: shutting down (ticks: %llu)", kPluginName,
                      (unsigned long long)g_tickCount.load(std::memory_order_relaxed));
    }

    virtual UInt32 QueryInterface(UInt32 id) override {
        (void)id;
        return 0;
    }
};

RCAIPlugin g_RCAI;

} // namespace

extern "C" bool F4SEPlugin_Load(const char* szVersion, const char* szCalculatedVersion) {
    return g_RCAI.Init(szVersion, szCalculatedVersion);
}
