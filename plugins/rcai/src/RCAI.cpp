/*
 * RCAI — Reactive Combat AI
 * =========================
 * F4SE plugin scaffold · FO4 Modernization Program · Milestone M1
 * (docs/MODERNIZATION_ROADMAP.md, pillar P0-b).
 *
 * v0.1 (this file) establishes the plugin foundation that every M1 feature
 * builds on:
 *
 *   - RCAI.ini configuration (Windows INI, same directory as the plugin DLL —
 *     build.yml ships RCAI.dll + config/RCAI.ini together)
 *   - a throttled tick loop on the F4SE "f4se::update" messaging channel
 *     (this is where the perception module, threat model and squad
 *     coordinator will hook in)
 *   - console commands:  rcai_status / rcai_toggle
 *   - F3 debug hotkey (input listener pattern for future debug overlays)
 *   - lifecycle logging to f4se.log via the F4SE plugin logger
 *
 * Targets F4SE 0.7.x (game 1.10.163.0, Next-Gen). The F4SE public API mirrors
 * the classic SKSE plugin template; if your F4SE header revision renames
 * anything, the compiler will point at the small number of calls below.
 */

#include <f4se/PluginAPI.h>
#include <f4se/PluginManager.h>
#include <f4se/PluginUtilities.h>
#include <f4se/ConsoleUtil.h>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>

namespace {

constexpr const char* kPluginName = "RCAI";
constexpr const char* kPluginVersion = "0.1.0";
constexpr const wchar_t* kIniFile = L"RCAI.ini";
constexpr const wchar_t* kIniSection = L"RCAI";
constexpr std::uint32_t kDebugKey = 0x32; // VK_F3

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

struct RCAIConfig
{
	bool   bEnabled          = true;   // master toggle
	std::int32_t iUpdateIntervalMS = 100; // tick throttle (0 = every frame)
	float  fPerceptionMult   = 1.0f;  // enemy perception multiplier (M1: perception module)
	bool   bDebugLogging     = false; // log every 1000 ticks to f4se.log
};

RCAIConfig g_config;

bool LoadConfig()
{
	const wchar_t* dir = L".";
	HMODULE hMod = nullptr;
	if (GetModuleHandleExW(
	        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	        L"RCAI", &hMod))
	{
		wchar_t path[MAX_PATH]{};
		if (GetModuleFileNameW(hMod, path, MAX_PATH) > 0)
		{
			std::wstring s(path);
			const size_t pos = s.find_last_of(L"\\/");
			if (pos != std::wstring::npos)
			{
				dir = s.substr(0, pos + 1).c_str();
			}
		}
	}

	const std::wstring iniPath(std::wstring(dir) + kIniFile);

	g_config.bEnabled =
		GetPrivateProfileIntW(kIniSection, L"bEnabled", 1, iniPath.c_str()) != 0;
	g_config.iUpdateIntervalMS =
		GetPrivateProfileIntW(kIniSection, L"iUpdateIntervalMS", 100, iniPath.c_str());
	g_config.fPerceptionMult =
		GetPrivateProfileFloatW(kIniSection, L"fPerceptionMult", 1.0f, iniPath.c_str());
	g_config.bDebugLogging =
		GetPrivateProfileIntW(kIniSection, L"bDebugLogging", 0, iniPath.c_str()) != 0;

	F4SE::LogInfo("%s: loaded config from %ls", kPluginName, iniPath.c_str());
	return true;
}

// ---------------------------------------------------------------------------
// runtime state (the M1 perception/threat systems will replace the tick body)
// ---------------------------------------------------------------------------

std::atomic<bool>        g_active{true};
std::atomic<std::uint64_t> g_tickCount{0};
std::uint64_t            g_lastTickMS = 0;
std::string              g_f4seVersion;

void Tick(const std::uint64_t now)
{
	g_tickCount.fetch_add(1, std::memory_order_relaxed);

	// M1 will run the perception battery / threat model here.
	if (g_config.bDebugLogging && (g_tickCount.load(std::memory_order_relaxed) % 1000 == 0))
	{
		const std::uint64_t n = g_tickCount.load(std::memory_order_relaxed);
		F4SE::LogInfo("%s: tick %llu enabled=%d perceptionMult=%.2f",
		              kPluginName,
		              (unsigned long long)n,
		              g_config.bEnabled ? 1 : 0,
		              g_config.fPerceptionMult);
	}
	(void)now;
}

void UpdateHandler(const F4SE::MessagingInterface::Message* msg)
{
	if (!msg || msg->type != F4SE::MessagingInterface::kMessage_Update)
	{
		return;
	}
	if (!g_config.bEnabled || !g_active.load(std::memory_order_acquire))
	{
		return;
	}
	const std::uint64_t now = GetTickCount64();
	if (g_config.iUpdateIntervalMS > 0 &&
	    now - g_lastTickMS < (std::uint64_t)g_config.iUpdateIntervalMS)
	{
		return;
	}
	g_lastTickMS = now;
	Tick(now);
}

void InputHandler(const F4SE::InputInterface::KeyData* keyData)
{
	if (!keyData || !keyData->isDown)
	{
		return;
	}
	if (keyData->keyCode == kDebugKey)
	{
		// Future: toggle RCAI debug overlay (threat heatmap, perception cones).
		F4SE::LogInfo("%s: F3 pressed (debug hotkey; overlay lands in M1)", kPluginName);
	}
}

// ---------------------------------------------------------------------------
// console commands
// ---------------------------------------------------------------------------

bool CmdRCAIStatus(const char** ret, const F4SE::ConsoleCommand::Args& args)
{
	(void)args;
	char buf[256];
	std::snprintf(buf, sizeof(buf),
	              "RCAI v%s (F4SE %s) | active=%s | ticks=%llu | "
	              "interval=%dms | perceptionMult=%.2f",
	              kPluginVersion,
	              g_f4seVersion.c_str(),
	              g_active.load(std::memory_order_acquire) ? "on" : "off",
	              (unsigned long long)g_tickCount.load(std::memory_order_relaxed),
	              g_config.iUpdateIntervalMS,
	              g_config.fPerceptionMult);
	*ret = buf;
	F4SE::LogInfo("%s: %s", kPluginName, buf);
	return true;
}

bool CmdRCAIToggle(const char** ret, const F4SE::ConsoleCommand::Args& args)
{
	(void)args;
	const bool now = !g_active.load(std::memory_order_acquire);
	g_active.store(now, std::memory_order_release);
	*ret = now ? "RCAI: tick processing enabled" : "RCAI: tick processing disabled";
	F4SE::LogInfo("%s: tick processing %s", kPluginName, now ? "enabled" : "disabled");
	return true;
}

// ---------------------------------------------------------------------------
// plugin callback
// ---------------------------------------------------------------------------

class RCAIPlugin : public F4SE::IPluginCallback
{
public:
	virtual bool Init(const char* version, const char* calculated) override
	{
		(void)calculated;
		using namespace F4SE;

		g_f4seVersion = version ? version : "?";

		if (!LoadConfig())
		{
			LogError("%s: could not read RCAI.ini, using defaults", kPluginName);
		}

		LogInfo("%s v%s: initializing (F4SE version %s)",
		        kPluginName, kPluginVersion, g_f4seVersion.c_str());

		ConsoleCommand::RegisterCommand(
			"rcai_status", "RCAI: show version, state and settings", &CmdRCAIStatus);
		ConsoleCommand::RegisterCommand(
			"rcai_toggle", "RCAI: enable/disable tick processing", &CmdRCAIToggle);

		GetMessaging().Register("f4se::update", &UpdateHandler);
		GetInput().RegisterListener(kDebugKey, &InputHandler);

		LogInfo("%s v%s: initialized", kPluginName, kPluginVersion);
		return true;
	}

	virtual void Shutdown(void) override
	{
		F4SE::LogInfo("%s: shutting down (ticks processed: %llu)",
		              kPluginName,
		              (unsigned long long)g_tickCount.load(std::memory_order_relaxed));
	}

	virtual UInt32 QueryInterface(UInt32 id) override
	{
		(void)id;
		return 0;
	}
};

RCAIPlugin g_RCAI;

} // namespace

extern "C" bool F4SEPlugin_Load(const char* szVersion, const char* szCalculatedVersion)
{
	return g_RCAI.Init(szVersion, szCalculatedVersion);
}
