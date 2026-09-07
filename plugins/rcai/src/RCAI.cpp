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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <string>
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
FILE* g_logFile = nullptr;

void Log(const char* fmt, ...) {
    if (!g_logFile) {
        wchar_t myDocs[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, myDocs))) {
            std::wstring logPath = std::wstring(myDocs) + L"\\My Games\\Fallout4\\F4SE\\RCAI.log";
            _wfopen_s(&g_logFile, logPath.c_str(), L"a");
        }
    }
    if (g_logFile) {
        va_list args;
        va_start(args, fmt);
        vfprintf(g_logFile, fmt, args);
        fprintf(g_logFile, "\n");
        fflush(g_logFile);
        va_end(args);
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
                  g_factionMemory.size(),
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
            }

            if (foundStatus && foundToggle) {
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
// task delegate & messaging
// ---------------------------------------------------------------------------

std::atomic<bool> g_gameReady{false};

class RCAITickTask : public ITaskDelegate {
public:
    virtual ~RCAITickTask() = default;

    void Run() override {
        if (!g_gameReady.load(std::memory_order_acquire)) {
            return;
        }

        const std::uint64_t now = GetTickCount64();
        if (g_config.bEnabled && g_active.load(std::memory_order_acquire)) {
            if (g_config.iUpdateIntervalMS <= 0 || (now - g_lastTickMS >= (std::uint64_t)g_config.iUpdateIntervalMS)) {
                g_lastTickMS = now;
#if defined(_WIN32)
                __try {
                    Tick(now);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    Log("RCAI: caught exception during Tick, skipping frame");
                }
#else
                try {
                    Tick(now);
                } catch (...) {}
#endif
            }
        }

        if (g_task && g_gameReady.load(std::memory_order_acquire)) {
            g_task->AddTask(new RCAITickTask());
        }
    }
};

void OnF4SEMessage(F4SEMessagingInterface::Message* msg) {
    if (!msg) return;
    Log("RCAI: F4SE message type %u", msg->type);

    if (msg->type == F4SEMessagingInterface::kMessage_PreLoadGame) {
        g_gameReady.store(false, std::memory_order_release);
    } else if (msg->type == F4SEMessagingInterface::kMessage_PostLoadGame ||
               msg->type == F4SEMessagingInterface::kMessage_NewGame) {
        Log("RCAI: game world active (msg type %u), starting tick task", msg->type);
        g_gameReady.store(true, std::memory_order_release);
        if (g_task) {
            g_task->AddTask(new RCAITickTask());
        }
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
    std::string cstyPath = pluginDir + "..\\..\\RCAI\\combat\\combat_styles.json";
    if (!std::ifstream(cstyPath).good()) {
        cstyPath = pluginDir + "..\\RCAI\\combat\\combat_styles.json";
    }
    if (!std::ifstream(cstyPath).good()) {
        cstyPath = "Data/RCAI/combat/combat_styles.json";
    }
    if (!std::ifstream(cstyPath).good()) {
        cstyPath = "data/combat/combat_styles.json";
    }
    g_sampler.init(cstyPath);

    Log("%s v%s: initialized (world sampler: 7/7 points wired; %zu styles loaded)",
        kPluginName, kPluginVersion, g_sampler.loadedStylesCount());

    g_messaging = (F4SEMessagingInterface*)f4se->QueryInterface(kInterface_Messaging);
    if (g_messaging) {
        g_messaging->RegisterListener(g_pluginHandle, "F4SE", OnF4SEMessage);
    }

    g_task = (F4SETaskInterface*)f4se->QueryInterface(kInterface_Task);

    RegisterConsoleCommands();

    Log("RCAI: plugin load complete");
    return true;
}

} // extern "C"
