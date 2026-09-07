#pragma once
// P2-a1 (runtime layer) · Faction memory.
//
// The full living-world simulation lives in tools/worldsim.py (it runs the
// decompiled 699-faction graph headlessly and ships its report). This module
// is the *in-game* runtime companion: a per-settlement faction memory that
// the adapter feeds with observed crimes/trades/raids and that the brain
// reads to modulate local behaviour (patrol intensity, hostility offsets).
//
// Persisted as JSON next to the plugin so memory survives restarts.

#include <map>
#include <string>
#include <vector>

#include "../util/json.h"

namespace rcai::worldsim {

struct SettlementMemory {
    std::string id;
    std::string ownerFaction;
    float attitude = 0.f;      // -1 (hostile) .. +1 (friendly), decays toward 0
    float trust = 0.5f;        // how quickly they believe bad news
    float lastEventTime = -1e9f;
    std::vector<std::string> recentEvents; // ring buffer of last 8 events
};

class FactionMemory {
public:
    SettlementMemory& settlement(const std::string& id, const std::string& owner) {
        auto it = mem_.find(id);
        if (it == mem_.end()) {
            SettlementMemory s;
            s.id = id;
            s.ownerFaction = owner;
            it = mem_.emplace(id, s).first;
        }
        return it->second;
    }

    const SettlementMemory* find(const std::string& id) const {
        const auto it = mem_.find(id);
        return it == mem_.end() ? nullptr : &it->second;
    }

    // Record an observed event; `impact` is -1..+1 (crime is negative).
    void record(const std::string& settlement, float impact, float time, const std::string& label) {
        auto it = mem_.find(settlement);
        if (it == mem_.end()) {
            SettlementMemory s;
            s.id = settlement;
            it = mem_.emplace(settlement, s).first;
        }
        auto& s = it->second;
        s.attitude = clamp01(s.attitude + impact * 0.35f);
        s.trust = clamp01(s.trust + (impact < 0 ? -0.05f : 0.02f));
        s.lastEventTime = time;
        s.recentEvents.push_back(label);
        if (s.recentEvents.size() > 8) s.recentEvents.erase(s.recentEvents.begin());
    }

    // Exponential forgetting toward neutral, called each game hour.
    void decay(float factor = 0.995f) {
        for (auto& kv : mem_) kv.second.attitude *= factor;
    }

    // Effective hostility modifier for AI in this settlement (-1..1).
    float hostility(const std::string& settlement) const {
        const auto* s = find(settlement);
        return s ? -s->attitude : 0.f;
    }

    size_t size() const { return mem_.size(); }

    size_t settlementCount() const {
        size_t c = 0;
        for (const auto& [id, _] : mem_) {
            if (id.rfind("faction:", 0) == std::string::npos) ++c;
        }
        return c;
    }

    std::vector<std::string> settlementNames() const {
        std::vector<std::string> names;
        for (const auto& [id, _] : mem_) {
            if (id.rfind("faction:", 0) == std::string::npos) names.push_back(id);
        }
        return names;
    }

    void loadSettlements(const std::string& text) {
        try {
            const json::Value v = json::Value::parse(text);
            const json::Value* arr = v.find("settlements");
            if (!arr || !arr->isArray()) return;
            for (const auto& s : arr->asArray()) {
                const std::string id = s.find("id") ? s.find("id")->asString() : "";
                if (id.empty()) continue;
                float rep = s.find("reputation") ? s.find("reputation")->asFloat() : 0.5f;
                auto& sm = settlement(id, s.find("kind") ? s.find("kind")->asString() : "");
                sm.trust = rep;
                sm.attitude = (rep - 0.5f) * 2.0f;
            }
        } catch (...) {}
    }

    void loadFactions(const std::string& text) {
        try {
            const json::Value v = json::Value::parse(text);
            const json::Value* arr = v.find("factions");
            if (!arr || !arr->isArray()) return;
            for (const auto& f : arr->asArray()) {
                const std::string edid = f.find("edid") ? f.find("edid")->asString() : "";
                const std::string cls = f.find("hostility_class") ? f.find("hostility_class")->asString() : "";
                if (edid.empty()) continue;
                float att = 0.f;
                if (cls == "Hostile") att = -0.8f;
                else if (cls == "Ally") att = 0.7f;
                else if (cls == "Friendly") att = 0.4f;
                settlement("faction:" + edid, edid).attitude = att;
            }
        } catch (...) {}
    }

    json::Value toJson() const {
        json::Value v;
        for (const auto& [id, s] : mem_) {
            json::Value e;
            e.set("owner", s.ownerFaction);
            e.set("attitude", s.attitude);
            e.set("trust", s.trust);
            e.set("last_event_time", s.lastEventTime);
            e.set("recent_events", json::Value(json::Array{
                [&] {
                    json::Array a;
                    for (const auto& ev : s.recentEvents) a.push_back(json::Value(ev));
                    return a;
                }()}));
            v.set(id, std::move(e));
        }
        return v;
    }

    // Seed attitudes from data/worldsim/factions.json (tools/worldsim.py):
    // {"factions": [{"edid": "...", "hostility_class": "Hostile|Ally|Friendly|Neutral|Isolated"}, ...]}
    static FactionMemory loadSeed(const std::string& text) {
        FactionMemory fm;
        fm.loadFactions(text);
        return fm;
    }

    static FactionMemory fromJson(const std::string& text) {
        FactionMemory fm;
        const json::Value v = json::Value::parse(text);
        for (const auto& [id, e] : v.asObject()) {
            auto& s = fm.settlement(id, e.find("owner") ? e.find("owner")->asString() : "");
            s.attitude = e.find("attitude") ? e.find("attitude")->asFloat() : 0.f;
            s.trust = e.find("trust") ? e.find("trust")->asFloat() : 0.5f;
            s.lastEventTime = e.find("last_event_time") ? e.find("last_event_time")->asFloat() : -1e9f;
        }
        return fm;
    }

private:
    static float clamp01(float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); }
    std::map<std::string, SettlementMemory> mem_;
};

} // namespace rcai::worldsim
