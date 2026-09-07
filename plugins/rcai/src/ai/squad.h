#pragma once
// P0-b2 (part 2) · Squad coordinator.
//
// Groups live hostile actors (proximity + faction), elects a leader, assigns
// roles, and redistributes on casualties. Also owns the *alert network*:
// threat sightings broadcast to the group with TTL so a single detection
// mobilises the whole squad (the stock engine has no such coordination).

#include <algorithm>
#include <map>

#include "world/world.h"

namespace rcai {

struct Alert {
    Vec2 pos{0, 0};
    float time = 0.f;
    float ttl = 20.f;
    bool valid() const { return time > 0.f; }
};

class SquadCoordinator {
public:
    struct Group {
        std::vector<std::string> members; // actor ids
        std::string leader;
        int suppressors = 0;
        int flankers = 0;
        int medics = 0;
        int heavies = 0;
        Alert alert;
        std::map<std::string, Role> roles;

        Role roleOf(const std::string& id) const {
            const auto it = roles.find(id);
            return it == roles.end() ? Role::None : it->second;
        }
    };

    void reset() { groups_.clear(); }

    // Rebuild groups from the live snapshot. Returns the group index of
    // `actorId`, or -1.
    int update(const WorldSnapshot& w, const std::string& actorId) {
        // Drop groups whose members all died.
        for (auto& g : groups_) {
            int alive = 0;
            for (const auto& m : g.members)
                if (const auto* a = w.findActor(m))
                    if (!a->isPlayer && a->health > 0.f) ++alive;
            g.members.erase(std::remove_if(g.members.begin(), g.members.end(),
                                           [&](const std::string& m) {
                                               const auto* a = w.findActor(m);
                                               return !a || a->health <= 0.f;
                                           }),
                            g.members.end());
        }

        // Find this actor's group: same faction, within grouping radius.
        const ActorState* self = w.findActor(actorId);
        if (!self || self->health <= 0.f || self->isPlayer) return -1;

        for (auto& g : groups_) {
            if (!g.members.empty()) {
                const ActorState* m0 = w.findActor(g.members.front());
                if (m0 && m0->factionIndex == self->factionIndex &&
                    distance(m0->pos, self->pos) <= kGroupRadius) {
                    if (std::find(g.members.begin(), g.members.end(), actorId) == g.members.end())
                        g.members.push_back(actorId);
                    reassignRoles(g);
                    return int(&g - groups_.data());
                }
            }
        }

        // New group (loner becomes its own leader).
        Group g;
        g.members.push_back(actorId);
        g.leader = actorId;
        groups_.push_back(std::move(g));
        reassignRoles(groups_.back());
        return int(groups_.size() - 1);
    }

    const Group* group(int i) const { return (i >= 0 && i < int(groups_.size())) ? &groups_[i] : nullptr; }
    size_t groupCount() const { return groups_.size(); }

    // Broadcast a threat sighting; whole group inherits the alert.
    void alert(const WorldSnapshot& w, const std::string& actorId, Vec2 pos) {
        const int gi = update(w, actorId);
        if (gi < 0) return;
        groups_[gi].alert = {pos, w.time, 20.f};
    }

    // Flank target: a position ~90 deg off the player's facing, at preferred
    // range, used by flanker roles and camp-counter probes.
    static Vec2 flankPosition(Vec2 playerPos, Vec2 playerFacing, Vec2 self, float range = 25.f) {
        const Vec2 perp{-playerFacing.z, playerFacing.x};
        // Pick the side the actor is already closer to (less travel).
        const Vec2 left = playerPos + perp * range;
        const Vec2 right = playerPos - perp * range;
        return distance(self, left) < distance(self, right) ? left : right;
    }

private:
    static constexpr float kGroupRadius = 150.f;
    std::vector<Group> groups_;

    void reassignRoles(Group& g) {
        // Leader: first member (stable) — in-game: highest level proxy.
        g.leader = g.members.empty() ? std::string{} : g.members.front();

        int n = int(g.members.size());
        g.suppressors = std::max(1, n / 2);
        g.flankers = n >= 3 ? std::min(2, n / 3) : 0;
        g.medics = n >= 4 ? 1 : 0;
        g.heavies = n >= 2 ? 1 : 0;
        // The plan must fit the squad: trim in priority order.
        int slots = std::max(0, n - 1); // leader takes no combat slot
        g.suppressors = std::min(g.suppressors, slots);
        slots -= g.suppressors;
        g.flankers = std::min(g.flankers, slots);
        slots -= g.flankers;
        g.medics = std::min(g.medics, slots);
        slots -= g.medics;
        g.heavies = std::min(g.heavies, slots);

        g.roles.clear();
        int assigned = 0;
        for (const auto& id : g.members) {
            Role r = Role::None;
            if (id == g.leader) {
                r = Role::Leader; // leader coordinates; takes no combat role slot
            } else {
                if (assigned < g.suppressors) {
                    r = Role::Suppressor;
                } else if (assigned < g.suppressors + g.flankers) {
                    r = Role::Flanker;
                } else if (assigned < g.suppressors + g.flankers + g.medics) {
                    r = Role::Medic;
                } else if (assigned < g.suppressors + g.flankers + g.medics + g.heavies) {
                    r = Role::Heavy;
                }
                ++assigned;
            }
            g.roles[id] = r;
        }
    }
};

} // namespace rcai
