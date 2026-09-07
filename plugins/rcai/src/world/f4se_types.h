#pragma once
// F4SE 0.7.x API Types & Definitions
// Grounded against F4SE 0.7.2 / runtime 1.10.163 headers.
// Safe for both Windows MSVC DLL builds and Linux CI syntax verification.

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

namespace rcai::f4se {

using UInt8 = std::uint8_t;
using UInt16 = std::uint16_t;
using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;
using SInt32 = std::int32_t;
using SInt64 = std::int64_t;

// Standard Bethesda 3D vector
struct NiPoint3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// F4SE dynamic array layout
template <typename T>
struct tArray {
    T* entries = nullptr;
    UInt32 count = 0;
    UInt32 pad04 = 0;
    UInt32 capacity = 0;
    UInt32 pad0C = 0;

    UInt32 size() const { return count; }
    bool empty() const { return count == 0 || entries == nullptr; }
    T& operator[](UInt32 idx) { return entries[idx]; }
    const T& operator[](UInt32 idx) const { return entries[idx]; }
};

// Form Types (matching GameForms.h)
enum FormType : UInt8 {
    kFormType_NONE = 0,
    kFormType_KYWD = 4,
    kFormType_FACT = 14,
    kFormType_ACTI = 27,
    kFormType_STAT = 36,
    kFormType_MSTT = 38,
    kFormType_NPC_ = 45,
    kFormType_CELL = 63,
    kFormType_REFR = 64,
    kFormType_ACHR = 65,
    kFormType_PACK = 82,
    kFormType_CSTY = 83,
};

// Common Actor Values
constexpr UInt32 kActorValue_Health = 0x000002D4;

// Base Form
class TESForm {
public:
    virtual ~TESForm() = default;

    UInt32 flags = 0;
    UInt32 formID = 0;
    UInt8 formType = kFormType_NONE;
    UInt8 pad0D[3]{};

    bool isDeleted() const { return (flags & 0x00000020) != 0; }
    bool isDisabled() const { return (flags & 0x00000800) != 0; }
};

class TESObjectCELL;

// Base Reference (matching GameReferences.h)
class TESObjectREFR : public TESForm {
public:
    virtual void Unk_01() {}
    virtual void Unk_02() {}

    TESObjectCELL* parentCell = nullptr;
    NiPoint3 rot{};
    float unkRotPad = 0.0f;
    NiPoint3 pos{};
    float unkPosPad = 0.0f;
    TESForm* baseForm = nullptr;
};

// Actor Value Data entry in Actor
struct ActorValueData {
    UInt32 avFormID = 0;
    float value = 0.0f;
};

class TESFaction : public TESForm {
public:
    SInt32 factionRank = 0;
};

class TESCombatStyle : public TESForm {
public:
    // CSTY raw floats (decompiled CSTY record data)
    float aggression = 0.5f;
    float confidence = 0.5f;
    float energyLevel = 0.5f;
    float coverPreference = 0.5f;
};

class TESNPC : public TESForm {
public:
    TESCombatStyle* combatStyle = nullptr;
    TESNPC* templateNPC = nullptr;
    tArray<TESFaction*> factions;
};

// Live Actor Character (matching GameReferences.h)
class Actor : public TESObjectREFR {
public:
    tArray<ActorValueData> actorValueData;
    TESFaction* vendorFaction = nullptr;
    UInt32 currentCombatTarget = 0;
    UInt32 actorFlags = 0;

    enum ActorFlagBits : UInt32 {
        kFlag_InCombat = (1 << 5),
        kFlag_Teammate = (1 << 26),
        kFlag_Dead = (1 << 20)
    };

    bool isInCombat() const { return (actorFlags & kFlag_InCombat) != 0 || currentCombatTarget != 0; }
    bool isDead() const { return (actorFlags & kFlag_Dead) != 0; }

    float getActorValue(UInt32 avId, float defaultVal = 100.0f) const {
        if (actorValueData.empty()) return defaultVal;
        for (UInt32 i = 0; i < actorValueData.count; ++i) {
            if (actorValueData[i].avFormID == avId) {
                return actorValueData[i].value;
            }
        }
        return defaultVal;
    }

    TESNPC* getNPC() const {
        if (!baseForm) return nullptr;
        if (baseForm->formType == kFormType_NPC_) {
            return reinterpret_cast<TESNPC*>(baseForm);
        }
        return nullptr;
    }

    virtual bool isHostileToActor(Actor* /*other*/) {
        return true; // Live hook: CALL_MEMBER_FN(this, IsHostileToActor)(other)
    }
};

// Player Character singleton
class PlayerCharacter : public Actor {
public:
    bool isFlashlightOn = false;
    float stealthNoise = 0.0f;
};

// Active Cell (matching GameForms.h)
class TESObjectCELL : public TESForm {
public:
    UInt16 cellFlags = 0;
    UInt16 unk42 = 0;
    tArray<TESObjectREFR*> objectList;
    void* land = nullptr;
    void* worldSpace = nullptr;

    bool isInterior() const { return (cellFlags & 1) != 0; }
};

// AI Packages for apply() integration
class TESPackage : public TESForm {
public:
    UInt32 procedureType = 0;
};

class AIUtilityPackage : public TESPackage {
public:
    enum UtilityAction : UInt32 {
        kAction_MoveTo = 1,
        kAction_Flank = 2,
        kAction_Retreat = 3,
        kAction_AidAlly = 4,
    };
    UtilityAction actionType = kAction_MoveTo;
    NiPoint3 targetPos{};
    UInt32 targetActorId = 0;
};

class AICombatPackage : public TESPackage {
public:
    enum CombatAction : UInt32 {
        kCombat_Attack = 1,
        kCombat_Suppress = 2,
    };
    CombatAction actionType = kCombat_Attack;
    UInt32 targetActorId = 0;
    TESFaction* faction = nullptr;
};

class AIFleePackage : public TESPackage {
public:
    NiPoint3 fleePoint{};
    float fleeTimer = 0.0f;
    UInt32 avoidActorId = 0;
};

// Virtual Machine for Papyrus dispatch
class VirtualMachine {
public:
    virtual ~VirtualMachine() = default;
    virtual void sendCustomEvent(const char* eventName, const char* arg1, const char* arg2) = 0;
};

// Global engine accessors
extern PlayerCharacter* g_playerInstance;
extern VirtualMachine* g_papyrusVM;

inline PlayerCharacter* getPlayer() {
    return g_playerInstance;
}

inline VirtualMachine* getVM() {
    return g_papyrusVM;
}

} // namespace rcai::f4se
