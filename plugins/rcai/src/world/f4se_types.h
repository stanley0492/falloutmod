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

// F4SE dynamic array layout (matching GameTypes.h)
template <typename T>
struct tArray {
    T* entries = nullptr;    // 00
    UInt32 capacity = 0;     // 08
    UInt32 pad0C = 0;        // 0C
    UInt32 count = 0;        // 10
    UInt32 pad14 = 0;        // 14

    UInt32 size() const { return count; }
    bool empty() const { return count == 0 || entries == nullptr; }
    T& operator[](UInt32 idx) { return entries[idx]; }
    const T& operator[](UInt32 idx) const { return entries[idx]; }
};
static_assert(offsetof(tArray<void*>, entries) == 0x00, "tArray entries offset");
static_assert(offsetof(tArray<void*>, capacity) == 0x08, "tArray capacity offset");
static_assert(offsetof(tArray<void*>, count) == 0x10, "tArray count offset");
static_assert(sizeof(tArray<void*>) == 0x18, "tArray size mismatch");

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

// Base Form (0x20 bytes)
class TESForm {
public:
    virtual ~TESForm() = default;

    void* unk08 = nullptr;
    UInt32 flags = 0;
    UInt32 formID = 0;
    UInt16 unk18 = 0;
    UInt8 formType = kFormType_NONE;
    UInt8 unk1B = 0;
    UInt32 pad1C = 0;

    bool isDeleted() const { return (flags & 0x00000020) != 0; }
    bool isDisabled() const { return (flags & 0x00000800) != 0; }
};
static_assert(sizeof(TESForm) == 0x20, "TESForm size mismatch");

class TESObjectCELL;

// Base Reference (0x110 bytes, matching GameReferences.h)
class TESObjectREFR : public TESForm {
public:
    UInt8 pad20[0xB8 - 0x20]{};
    TESObjectCELL* parentCell = nullptr; // 0xB8
    NiPoint3 rot{};                      // 0xC0
    float unkCC = 0.0f;                  // 0xCC
    NiPoint3 pos{};                      // 0xD0
    float unkDC = 0.0f;                  // 0xDC
    TESForm* baseForm = nullptr;         // 0xE0
    UInt8 padE8[0x110 - 0xE8]{};         // 0xE8
};
static_assert(offsetof(TESObjectREFR, parentCell) == 0xB8, "parentCell offset mismatch");
static_assert(offsetof(TESObjectREFR, rot) == 0xC0, "rot offset mismatch");
static_assert(offsetof(TESObjectREFR, pos) == 0xD0, "pos offset mismatch");
static_assert(offsetof(TESObjectREFR, baseForm) == 0xE0, "baseForm offset mismatch");
static_assert(sizeof(TESObjectREFR) == 0x110, "TESObjectREFR size mismatch");

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
    UInt8 pad110[0x2D0 - 0x110]{};
    UInt32 actorFlags = 0;               // 0x2D0
    tArray<ActorValueData> actorValueData; // 0x2D8 for test harness
    TESFaction* vendorFaction = nullptr;
    UInt32 currentCombatTarget = 0;

    enum ActorFlagBits : UInt32 {
        kFlag_InCombat = (1 << 5),
        kFlag_Dead = (1 << 20),
        kFlag_Teammate = (1 << 26)
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
static_assert(offsetof(Actor, actorFlags) == 0x2D0, "actorFlags offset mismatch");

// Player Character singleton
class PlayerCharacter : public Actor {
public:
    bool isFlashlightOn = false;
    float stealthNoise = 0.0f;
};

// Active Cell (0xF0 bytes, matching GameForms.h)
class TESObjectCELL : public TESForm {
public:
    UInt8 pad20[0x40 - 0x20]{};
    UInt16 cellFlags = 0;                // 0x40
    UInt16 unk42 = 0;                    // 0x42
    UInt8 pad44[0x70 - 0x44]{};          // pad to 0x70
    tArray<TESObjectREFR*> objectList;   // 0x70
    UInt8 pad88[0xF0 - 0x88]{};          // pad to 0xF0

    bool isInterior() const { return (cellFlags & 1) != 0; }
};
static_assert(offsetof(TESObjectCELL, cellFlags) == 0x40, "cellFlags offset mismatch");
static_assert(offsetof(TESObjectCELL, objectList) == 0x70, "objectList offset mismatch");
static_assert(sizeof(TESObjectCELL) == 0xF0, "TESObjectCELL size mismatch");

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
