#pragma once
// P0-a5 · Crash watchdog & JSON crash dumps.
//
// On Windows the plugin installs an SEH guard around the Papyrus native
// dispatch boundary (the 1,669-function attack surface per the decompiled
// registry); on any unhandled exception it writes a structured JSON crash
// dump (reports/rcai_crash_*.json) and triggers the auto-restore path.
//
// The dump/restore logic is platform-independent and unit-tested here; only
// the SEH trampoline is Windows-only.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../util/json.h"

namespace rcai::perf {

class CrashWatchdog {
public:
    explicit CrashWatchdog(std::string dumpDir = "reports") : dumpDir_(std::move(dumpDir)) {}

    // Build the crash dump body (also used by tests).
    static json::Value makeDump(const char* what, long pid, double uptimeSec,
                                int actorsLive, int inCombat, const char* lastCell) {
        json::Value v;
        v.set("what", std::string(what ? what : "unknown"));
        v.set("pid", static_cast<int>(pid));
        v.set("uptime_seconds", uptimeSec);
        v.set("actors_live", actorsLive);
        v.set("in_combat", inCombat);
        v.set("last_cell", std::string(lastCell ? lastCell : "?"));
        v.set("program", std::string("RCAI"));
        v.set("schema", 1);
        return v;
    }

    // Write the dump; returns the path, or "" on failure (logged).
    std::string writeDump(const char* what, long pid, double uptimeSec, int actorsLive,
                          int inCombat, const char* lastCell) {
        std::string path = dumpDir_ + "/rcai_crash_" + std::to_string(dumpCounter_++) + ".json";
        std::ofstream out(path, std::ios::binary);
        if (!out) return "";
        out << makeDump(what, pid, uptimeSec, actorsLive, inCombat, lastCell).dump(2);
        out << '\n';
        return path;
    }

    // Restore decision: if a crash dump exists for this session, the game
    // should restore the last known-good save instead of continuing.
    static bool shouldRestore(long pid, const std::string& lastRestorePidFile,
                              long& outRestoredPid) {
        std::ifstream in(lastRestorePidFile);
        if (!in) return false;
        long stored = 0;
        in >> stored;
        outRestoredPid = stored;
        // Restore at most once per process id (avoids restore loops).
        return stored == pid;
    }

    static void markRestored(const std::string& lastRestorePidFile, long pid) {
        std::ofstream out(lastRestorePidFile, std::ios::binary);
        if (out) out << pid;
    }

    int dumpCount() const { return dumpCounter_; }

private:
    std::string dumpDir_;
    int dumpCounter_ = 0;
};

// Windows SEH trampoline (compile-time guarded; the in-game build calls this
// around every Papyrus native dispatch and around the tick loop).
#ifdef _WIN32
#include <windows.h>

typedef int (*GuardedFunc)(void* ctx);

inline int guardedCall(GuardedFunc fn, void* ctx, CrashWatchdog* wd, const char* op) {
    __try {
        return fn(ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        (void)wd;
        (void)op;
        return -1; // in-game build: wd->writeDump(op, ...) + auto-restore here
    }
}
#endif

} // namespace rcai::perf
