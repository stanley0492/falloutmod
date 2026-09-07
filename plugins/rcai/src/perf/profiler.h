#pragma once
// P0-a1 · Frame profiler.
//
// Tracks per-subsystem frame times and exports CSV consumed by
// tools/perf_report.py (markdown summary + regression gates). In the in-game
// build, subsystem scopes are marked at the RTTI-identified hot paths (see
// docs/INTEGRATION_CHECKLIST.md); tests mark them directly.

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace rcai::perf {

class FrameProfiler {
public:
    struct Scope {
        FrameProfiler* prof;
        const char* name;
        std::chrono::steady_clock::time_point start;
        explicit Scope(FrameProfiler* p, const char* n) : prof(p), name(n), start(clock::now()) {}
        ~Scope() {
            if (prof)
                prof->record(name, std::chrono::duration<double, std::milli>(clock::now() - start).count());
        }
    };

    void setSubsystems(std::vector<std::string> names) { subsystemOrder_ = std::move(names); }

    void beginFrame() {
        frameStart_ = clock::now();
        ++frameIndex_;
    }

    void record(const std::string& subsystem, double ms) {
        accum_[subsystem].sum += ms;
        accum_[subsystem].count++;
        if (ms > accum_[subsystem].max) accum_[subsystem].max = ms;
    }

    void endFrame(double frameMs = -1) {
        const double f = frameMs < 0
                             ? std::chrono::duration<double, std::milli>(clock::now() - frameStart_).count()
                             : frameMs;
        frameTimes_.push_back(f);

        // One CSV row: all subsystems active this frame (ordered, zero-filled).
        std::ostringstream row;
        row << frameIndex_;
        const auto order = orderedSubsystems();
        for (const auto& name : order) {
            const auto it = accum_.find(name);
            row << ',' << (it != accum_.end() ? it->second.sum : 0.0);
        }
        row << ',' << f << '\n';
        csv_ += row.str();

        for (auto& kv : accum_) {
            kv.second.sum = 0;
            kv.second.count = 0;
            kv.second.max = 0;
        }
    }

    void exportCsv(const std::string& path) const {
        std::ofstream out(path);
        if (!out) return;
        out << "frame";
        for (const auto& s : orderedSubsystems()) out << ',' << s;
        out << ",frame_ms\n";
        out << csv_;
    }

    struct Summary {
        int frames = 0;
        double medianMs = 0, p95Ms = 0, minMs = 1e300, maxMs = 0;
    };
    Summary summary() const {
        Summary s;
        s.frames = int(frameTimes_.size());
        if (s.frames == 0) return s;
        std::vector<double> sorted = frameTimes_;
        std::sort(sorted.begin(), sorted.end());
        s.medianMs = sorted[s.frames / 2];
        s.p95Ms = sorted[std::min(s.frames - 1, static_cast<int>(s.frames * 0.95))];
        s.minMs = sorted.front();
        s.maxMs = sorted.back();
        return s;
    }

private:
    using clock = std::chrono::steady_clock;
    struct Acc { double sum = 0; long count = 0; double max = 0; };

    std::vector<std::string> orderedSubsystems() const {
        std::vector<std::string> names;
        for (const auto& s : subsystemOrder_)
            if (accum_.count(s) || !names.empty() || !csv_.empty()) names.push_back(s);
        if (names.empty())
            for (const auto& kv : accum_) names.push_back(kv.first);
        return names;
    }

    clock::time_point frameStart_;
    long frameIndex_ = 0;
    std::map<std::string, Acc> accum_;
    std::vector<std::string> subsystemOrder_;
    std::vector<double> frameTimes_;
    std::string csv_;
};

} // namespace rcai::perf
