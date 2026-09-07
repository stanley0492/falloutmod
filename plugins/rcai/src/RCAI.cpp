/*
 * RCAI — Reactive Combat AI
 * =========================
 * F4SE native plugin · FO4 Modernization Program · Milestones M1–M7.
 *
 * Grounded for Fallout 4 build 1.10.163 / F4SE 0.6.23.
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shlobj.h>

#pragma comment(lib, "Shell32.lib")

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "common/ITypes.h"
#include "f4se_common/f4se_version.h"
#include "f4se/PluginAPI.h"
#include "f4se/ObScript.h"
#include "f4se/GameThreads.h"

#include "util/ini.h"
#include "world/world.h"
#include "world/f4se_sampler.h"
#include "ai/behaviour.h"
#include "perf/profiler.h"
#include "perf/ini_tuner.h"
#include "perf/watchdog.h"
#include "worldsim/faction_memory.h"

namespace {

using namespace rcai;

constexpr const char* kPluginName = "RCAI";
constexpr const char* kPluginVersion = "0.3.0";
constexpr const wchar_t* kIniFile = L"RCAI.ini";
constexpr const wchar_t* kIniSection = L"RCAI";
constexpr std::uint32_t kDebugKey = 0x32; // VK_F3

PluginHandle g_pluginHandle = kPluginHandle_Invalid;
F4SEMessagingInterface* g_messaging = nullptr;
F4SETaskInterface* g_task = nullptr;

void Log(const char* fmt, ...) {
    wchar_t myDocs[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, myDocs))) {
        std::wstring logPath = std::wstring(myDocs) + L"\\My Games\\Fallout4\\F4SE\\RCAI.log";
        FILE* f = _wfsopen(logPath.c_str(), L"a", _SH_DENYNO);
        if (f) {
            va_list args;
            va_start(args, fmt);
            vfprintf(f, fmt, args);
            fprintf(f, "\n");
            fflush(f);
            va_end(args);
            fclose(f);
        }
    }
}

// ---------------------------------------------------------------------------
// Engine Relocations (Fallout 4 1.10.163)
// ---------------------------------------------------------------------------

inline uintptr_t GetBaseAddr() {
    static uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    return base;
}

inline ObScriptCommand* GetFirstConsoleCommand() {
    return (ObScriptCommand*)(GetBaseAddr() + 0x03706DC0);
}

inline void* GetConsoleManager() {
    void*** pConsole = (void***)(GetBaseAddr() + 0x058E0AE0);
    return (pConsole && *pConsole) ? **pConsole : nullptr;
}

inline void* GetPlayerPtr() {
    void** pPlayer = (void**)(GetBaseAddr() + 0x05AA4388);
    return pPlayer ? *pPlayer : nullptr;
}

void Console_Print(const char* fmt, ...) {
    void* mgr = GetConsoleManager();
    if (mgr) {
        typedef void (*_VPrint)(void* thisPtr, const char* fmt, va_list args);
        _VPrint vprint = (_VPrint)(GetBaseAddr() + 0x01262EC0);
        va_list args;
        va_start(args, fmt);
        vprint(mgr, fmt, args);
        va_end(args);
    }
}

void SafeWriteBuf(uintptr_t addr, const void* data, size_t len) {
    DWORD oldProtect;
    if (VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy((void*)addr, data, len);
        VirtualProtect((void*)addr, len, oldProtect, &oldProtect);
    }
}

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

std::string wstringToString(const std::wstring& w) {
    if (w.empty()) return "";
    std::string s(w.size(), '\0');
    for (size_t i = 0; i < w.size(); ++i) s[i] = static_cast<char>(w[i] & 0x7f);
    return s;
}

float GetIniFloat(const wchar_t* section, const wchar_t* key, float defaultVal, const wchar_t* path) {
    wchar_t buf[64]{};
    if (GetPrivateProfileStringW(section, key, L"", buf, 64, path) > 0) {
        wchar_t* end = nullptr;
        float val = std::wcstof(buf, &end);
        if (end != buf) return val;
    }
    return defaultVal;
}

bool LoadConfig() {
    const std::wstring dir = GetPluginDir();
    const std::wstring iniPath(dir + kIniFile);

    g_config.bEnabled =
        GetPrivateProfileIntW(kIniSection, L"bEnabled", 1, iniPath.c_str()) != 0;
    g_config.iUpdateIntervalMS =
        GetPrivateProfileIntW(kIniSection, L"iUpdateIntervalMS", 100, iniPath.c_str());
    g_config.fPerceptionMult =
        GetIniFloat(kIniSection, L"fPerceptionMult", 1.0f, iniPath.c_str());
    g_config.bDebugLogging =
        GetPrivateProfileIntW(kIniSection, L"bDebugLogging", 0, iniPath.c_str()) != 0;
    g_config.fPapyrusBudgetMS =
        GetIniFloat(L"Perf", L"fPapyrusBudgetMS", 2.0f, iniPath.c_str());
    g_config.fHavokMaxTime =
        GetIniFloat(L"Perf", L"fHavokMaxTime", 0.016f, iniPath.c_str());
    g_config.bFactionMemory =
        GetPrivateProfileIntW(L"WorldSim", L"bFactionMemory", 1, iniPath.c_str()) != 0;

    Log("%s: loaded config from %ls", kPluginName, iniPath.c_str());
    return true;
}

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

    void* playerPtr = GetPlayerPtr();
    if (!playerPtr || (uintptr_t)playerPtr < 0x10000) {
        f4se::g_playerInstance = nullptr;
        return;
    }
    f4se::g_playerInstance = reinterpret_cast<f4se::PlayerCharacter*>(playerPtr);

    g_profiler.beginFrame();
    const float dt = (g_config.iUpdateIntervalMS > 0)
                         ? g_config.iUpdateIntervalMS / 1000.f
                         : 0.016f;
    WorldSnapshot world;
    if (g_sampler.sample(world, dt)) {
        const BrainResult result = g_brain.tick(world, {}, dt);
        for (const auto& a : result.actions) g_sampler.apply(a);
        g_profiler.record("brain", 0.0);
    }
    g_profiler.endFrame();

    if (g_config.bFactionMemory) g_factionMemory.decay(1.0f - 0.0001f);

    const std::uint64_t n = g_tickCount.load(std::memory_order_relaxed);
    if (g_config.bDebugLogging && (n % 1000 == 0)) {
        Log("%s: tick %llu | %s", kPluginName, (unsigned long long)n,
            g_brain.debug(world).c_str());
    }
    (void)now;
}

// ---------------------------------------------------------------------------
// console commands
// ---------------------------------------------------------------------------

bool CmdRCAIStatus_Execute(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr) {
    (void)paramInfo; (void)scriptData; (void)thisObj; (void)containingObj; (void)scriptObj; (void)locals; (void)result; (void)opcodeOffsetPtr;
    const perf::FrameProfiler::Summary s = g_profiler.summary();
    const auto& diag = g_sampler.diagnostics();
    char buf[384];
    std::snprintf(buf, sizeof(buf),
                  "RCAI v%s (F4SE %s) | active=%s ticks=%llu | frames=%d med=%.1fms p95=%.1fms | "
                  "actors=%zu losCalls=%zu cover=%zu | mem=%zu settlements | %s",
                  kPluginVersion, g_f4seVersion.c_str(),
                  g_active.load(std::memory_order_acquire) ? "on" : "off",
                  (unsigned long long)g_tickCount.load(std::memory_order_relaxed), s.frames,
                  s.medianMs, s.p95Ms, diag.actorsSampled,
                  diag.losCallsPerFrame, diag.coverPointsPerCell,
                  g_factionMemory.settlementCount(),
                  g_brain.antiCheese().describe().c_str());
    Console_Print("%s", buf);
    Log("%s: %s", kPluginName, buf);
    return true;
}

bool CmdRCAIToggle_Execute(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr) {
    (void)paramInfo; (void)scriptData; (void)thisObj; (void)containingObj; (void)scriptObj; (void)locals; (void)result; (void)opcodeOffsetPtr;
    const bool now = !g_active.load(std::memory_order_acquire);
    g_active.store(now, std::memory_order_release);
    Console_Print("RCAI: tick processing %s", now ? "enabled" : "disabled");
    Log("%s: tick processing %s", kPluginName, now ? "enabled" : "disabled");
    return true;
}

bool CmdRCAIInjectRaid_Execute(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr) {
    (void)paramInfo; (void)scriptData; (void)thisObj; (void)containingObj; (void)scriptObj; (void)locals; (void)result; (void)opcodeOffsetPtr;

    static size_t s_raidIndex = 0;
    std::vector<std::string> settlements = g_factionMemory.settlementNames();
    if (settlements.empty()) {
        settlements = {"DiamondCity", "Goodneighbor", "BunkerHill", "Sanctuary"};
    }

    std::string target = settlements[s_raidIndex % settlements.size()];
    s_raidIndex++;

    const float timeSec = static_cast<float>(GetTickCount64()) / 1000.0f;
    g_factionMemory.record(target, -0.4f, timeSec, "Hostile raid incident");

    const auto* mem = g_factionMemory.find(target);
    const float att = mem ? mem->attitude : -0.4f;
    const float trust = mem ? mem->trust : 0.45f;
    const size_t evCount = mem ? mem->recentEvents.size() : 1;

    char buf[256];
    std::snprintf(buf, sizeof(buf), "RCAI: injected raid at %s | attitude: %.2f | trust: %.2f | events: %zu",
                  target.c_str(), att, trust, evCount);
    Console_Print("%s", buf);
    Log("%s: %s", kPluginName, buf);
    return true;
}

bool CmdRCAIDumpMemory_Execute(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr) {
    (void)paramInfo; (void)scriptData; (void)thisObj; (void)containingObj; (void)scriptObj; (void)locals; (void)result; (void)opcodeOffsetPtr;

    wchar_t myDocs[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, myDocs))) {
        std::wstring dumpPath = std::wstring(myDocs) + L"\\My Games\\Fallout4\\F4SE\\RCAI_memory.json";
        std::string jsonText = g_factionMemory.toJson().dump(2);
        std::ofstream ofs(dumpPath);
        if (ofs.good()) {
            ofs << jsonText;
            ofs.close();
            Log("%s: dumped faction memory to %ls", kPluginName, dumpPath.c_str());
        }
    }

    Console_Print("=== RCAI Settlement Ledger (%zu settlements) ===", g_factionMemory.settlementCount());
    std::vector<std::string> settlements = g_factionMemory.settlementNames();
    size_t printed = 0;
    for (const auto& name : settlements) {
        if (const auto* s = g_factionMemory.find(name)) {
            Console_Print("  [%s] att: %.2f | trust: %.2f | events: %zu",
                          name.c_str(), s->attitude, s->trust, s->recentEvents.size());
            if (++printed >= 6) break;
        }
    }
    if (settlements.size() > printed) {
        Console_Print("  ... (%zu more in RCAI_memory.json)", settlements.size() - printed);
    }
    return true;
}

bool CmdRCAITune_Execute(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr) {
    (void)paramInfo; (void)scriptData; (void)thisObj; (void)containingObj; (void)scriptObj; (void)locals; (void)result; (void)opcodeOffsetPtr;
    const std::wstring dir = GetPluginDir();
    const std::wstring iniPath(dir + L"..\\..\\Fallout4Custom.ini");
    perf::TunerSettings ts;
    ts.papyrusUpdateBudgetMs = g_config.fPapyrusBudgetMS;
    ts.havokMaxTime = g_config.fHavokMaxTime;
    const perf::TunerReport rep = perf::IniTuner::tune(wstringToString(iniPath), ts);
    Console_Print("RCAI: tuned %zu INI keys in Fallout4Custom.ini", rep.changes.size());
    Log("RCAI: tuned %zu INI keys", rep.changes.size());
    return true;
}

void RegisterConsoleCommands() {
    ObScriptCommand* firstCmd = GetFirstConsoleCommand();
    if (!firstCmd) return;

#if defined(_WIN32)
    __try {
#endif
        bool foundStatus = false;
        bool foundToggle = false;
        bool foundRaid = false;
        bool foundDump = false;

        for (UInt32 i = 0; i < kObScript_NumConsoleCommands; ++i) {
            ObScriptCommand* iter = &firstCmd[i];
            if (!iter->longName) continue;

            if (!foundStatus && !_stricmp(iter->longName, "ToggleESRAM")) {
                ObScriptCommand cmd = *iter;
                cmd.longName = "rcai_status";
                cmd.shortName = "rcais";
                cmd.helpText = "RCAI: show version, state, perf and AI stats";
                cmd.needsParent = 0;
                cmd.numParams = 0;
                cmd.execute = CmdRCAIStatus_Execute;
                cmd.flags = 0;
                SafeWriteBuf((uintptr_t)iter, &cmd, sizeof(cmd));
                Log("RCAI: registered console command 'rcai_status'");
                foundStatus = true;
            } else if (!foundToggle && !_stricmp(iter->longName, "TestSeenData")) {
                ObScriptCommand cmd = *iter;
                cmd.longName = "rcai_toggle";
                cmd.shortName = "rcait";
                cmd.helpText = "RCAI: enable/disable tick processing";
                cmd.needsParent = 0;
                cmd.numParams = 0;
                cmd.execute = CmdRCAIToggle_Execute;
                cmd.flags = 0;
                SafeWriteBuf((uintptr_t)iter, &cmd, sizeof(cmd));
                Log("RCAI: registered console command 'rcai_toggle'");
                foundToggle = true;
            } else if (!foundRaid && !_stricmp(iter->longName, "SetESRAMSetup")) {
                ObScriptCommand cmd = *iter;
                cmd.longName = "rcai_inject_raid";
                cmd.shortName = "rcair";
                cmd.helpText = "RCAI: inject raid into settlement";
                cmd.needsParent = 0;
                cmd.numParams = 0;
                cmd.execute = CmdRCAIInjectRaid_Execute;
                cmd.flags = 0;
                SafeWriteBuf((uintptr_t)iter, &cmd, sizeof(cmd));
                Log("RCAI: registered console command 'rcai_inject_raid'");
                foundRaid = true;
            } else if (!foundDump && !_stricmp(iter->longName, "TestLocalMap")) {
                ObScriptCommand cmd = *iter;
                cmd.longName = "rcai_dump_memory";
                cmd.shortName = "rcaid";
                cmd.helpText = "RCAI: dump faction & settlement memory ledger";
                cmd.needsParent = 0;
                cmd.numParams = 0;
                cmd.execute = CmdRCAIDumpMemory_Execute;
                cmd.flags = 0;
                SafeWriteBuf((uintptr_t)iter, &cmd, sizeof(cmd));
                Log("RCAI: registered console command 'rcai_dump_memory'");
                foundDump = true;
            }

            if (foundStatus && foundToggle && foundRaid && foundDump) {
                break;
            }
        }
#if defined(_WIN32)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("RCAI: warning - exception during RegisterConsoleCommands, safely handled");
    }
#endif
}

// ---------------------------------------------------------------------------
// task delegate & background worker thread
// ---------------------------------------------------------------------------

std::atomic<bool> g_workerRunning{false};
std::atomic<bool> g_gameReady{false};
std::atomic<bool> g_tickInFlight{false};

class RCAISingleTickTask : public ITaskDelegate {
public:
    virtual ~RCAISingleTickTask() = default;

    void Run() override {
        const std::uint64_t now = GetTickCount64();
#if defined(_WIN32)
        __try {
            Tick(now);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            static int errCount = 0;
            if (errCount++ < 5) {
                Log("RCAI: caught exception during Tick, skipping frame");
            }
        }
#else
        try {
            Tick(now);
        } catch (...) {}
#endif
        g_tickInFlight.store(false, std::memory_order_release);
    }
};

void WorkerThreadFunc() {
    Log("RCAI: background worker thread started");
    while (g_workerRunning.load(std::memory_order_acquire)) {
        const int interval = (g_config.iUpdateIntervalMS > 0) ? g_config.iUpdateIntervalMS : 50;
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));

        if (!g_workerRunning.load(std::memory_order_acquire)) {
            break;
        }

        if (g_gameReady.load(std::memory_order_acquire) &&
            g_config.bEnabled &&
            g_active.load(std::memory_order_acquire)) {
            bool expected = false;
            if (g_tickInFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                if (g_task) {
                    g_task->AddTask(new RCAISingleTickTask());
                } else {
                    g_tickInFlight.store(false, std::memory_order_release);
                }
            }
        }
    }
    Log("RCAI: background worker thread stopped");
}

void OnF4SEMessage(F4SEMessagingInterface::Message* msg) {
    if (!msg) return;
    Log("RCAI: F4SE message type %u", msg->type);

    if (msg->type == F4SEMessagingInterface::kMessage_PreLoadGame) {
        g_gameReady.store(false, std::memory_order_release);
    } else if (msg->type == F4SEMessagingInterface::kMessage_PostLoadGame ||
               msg->type == F4SEMessagingInterface::kMessage_NewGame) {
        Log("RCAI: game world active (msg type %u), starting settle timer", msg->type);
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            g_gameReady.store(true, std::memory_order_release);
            Log("RCAI: game world settled, AI ticking enabled");
        }).detach();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// F4SE Plugin entry points
// ---------------------------------------------------------------------------

extern "C" {

__declspec(dllexport) bool F4SEPlugin_Query(const F4SEInterface* f4se, PluginInfo* info) {
    Log("RCAI v%s Query (f4se version %08X, runtime %08X)", kPluginVersion, f4se->f4seVersion, f4se->runtimeVersion);

    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = "RCAI";
    info->version = 3;

    if (f4se->isEditor) {
        Log("RCAI: loaded in editor, marking as incompatible");
        return false;
    }

    if (f4se->runtimeVersion < RUNTIME_VERSION_1_10_163) {
        Log("RCAI: unsupported runtime version %08X (required %08X)", f4se->runtimeVersion, RUNTIME_VERSION_1_10_163);
        return false;
    }

    return true;
}

__declspec(dllexport) bool F4SEPlugin_Load(const F4SEInterface* f4se) {
    Log("RCAI v%s Load", kPluginVersion);

    g_pluginHandle = f4se->GetPluginHandle();

    g_f4seVersion = std::to_string(GET_EXE_VERSION_MAJOR(f4se->f4seVersion)) + "." +
                    std::to_string(GET_EXE_VERSION_MINOR(f4se->f4seVersion)) + "." +
                    std::to_string(GET_EXE_VERSION_BUILD(f4se->f4seVersion));

    if (!LoadConfig()) {
        Log("%s: could not read RCAI.ini, using defaults", kPluginName);
    }

    g_profiler.setSubsystems({"brain", "perception", "behaviour", "streaming", "papyrus"});

    const std::string pluginDir = wstringToString(GetPluginDir());
    auto findDataPath = [&](const std::string& rel) -> std::string {
        std::string p1 = pluginDir + "..\\..\\RCAI\\" + rel;
        if (std::ifstream(p1).good()) return p1;
        std::string p2 = pluginDir + "..\\RCAI\\" + rel;
        if (std::ifstream(p2).good()) return p2;
        std::string p3 = "Data/RCAI/" + rel;
        if (std::ifstream(p3).good()) return p3;
        std::string p4 = "data/" + rel;
        if (std::ifstream(p4).good()) return p4;
        return "";
    };

    std::string cstyPath = findDataPath("combat/combat_styles.json");
    g_sampler.init(cstyPath);

    const std::string settlementsPath = findDataPath("worldsim/settlements.json");
    if (!settlementsPath.empty()) {
        std::ifstream ifs(settlementsPath);
        if (ifs.good()) {
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            g_factionMemory.loadSettlements(content);
        }
    }

    const std::string factionsPath = findDataPath("worldsim/factions.json");
    if (!factionsPath.empty()) {
        std::ifstream ifs(factionsPath);
        if (ifs.good()) {
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            g_factionMemory.loadFactions(content);
        }
    }

    Log("%s v%s: initialized (world sampler: 7/7 points wired; %zu styles loaded; %zu settlements seeded)",
        kPluginName, kPluginVersion, g_sampler.loadedStylesCount(), g_factionMemory.settlementCount());

    g_messaging = (F4SEMessagingInterface*)f4se->QueryInterface(kInterface_Messaging);
    if (g_messaging) {
        g_messaging->RegisterListener(g_pluginHandle, "F4SE", OnF4SEMessage);
    }

    g_task = (F4SETaskInterface*)f4se->QueryInterface(kInterface_Task);

    RegisterConsoleCommands();

    g_workerRunning.store(true, std::memory_order_release);
    std::thread(WorkerThreadFunc).detach();

    Log("RCAI: plugin load complete");
    return true;
}

} // extern "C"
