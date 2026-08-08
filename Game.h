#pragma once

#include <cstdint>

//
// World of Warcraft 2.4.3 (build 8606) port.
//
// All addresses below were resolved by static analysis of a stock 8606 Wow.exe
// (ImageBase 0x400000, sha1 a802af90d44c08875fa6949239044afa1a488f92).
// If your Wow.exe hashes differently, re-verify before use.
//
// See OFFSETS-2.4.3.md for how each value was derived.
//

// TYPEMASK values used by ClntObjMgrObjectPtr (bitmask, not an enum index).
enum TypeMask
{
    TYPEMASK_OBJECT        = 0x01,
    TYPEMASK_ITEM          = 0x02,
    TYPEMASK_CONTAINER     = 0x04,
    TYPEMASK_UNIT          = 0x08,
    TYPEMASK_PLAYER        = 0x10,
    TYPEMASK_GAMEOBJECT    = 0x20,
    TYPEMASK_DYNAMICOBJECT = 0x40,
    TYPEMASK_CORPSE        = 0x80,
};

enum Offsets
{
    // --- Object manager -------------------------------------------------
    // uint32 __cdecl ClntObjMgrObjectPtr(uint64 guid, uint32 typeMask,
    //                                    const char* file, int line)
    FUN_OBJECT_POINTER      = 0x46B610,

    // int __cdecl ClntObjMgrEnumVisibleObjects(ENUM_CALLBACK cb, void* userdata)
    FUN_ENUM_VISIBLE        = 0x46B3F0,

    // uint64 __cdecl ClntObjMgrGetActivePlayerGuid()   (returns in edx:eax)
    FUN_ACTIVE_PLAYER_GUID  = 0x469DD0,

    // void* __cdecl ClntObjMgrGetCurMgr()  -- NULL when not in world
    FUN_GET_CUR_MGR         = 0x469D70,

    // --- Interaction ----------------------------------------------------
    // void __cdecl CGGameUI::SetTarget(uint64 guid)
    FUN_SET_TARGET          = 0x4A6690,

    // --- FrameScript (Lua) ----------------------------------------------
    // int __cdecl FrameScript_IsString(lua_State* L, int index)
    FUN_LUA_ISSTRING        = 0x72DE70,

    // const char* __cdecl FrameScript_GetText(lua_State* L, int index, int* len)
    FUN_LUA_GETTEXT         = 0x72DFF0,

    // Lua handler for UnitXP(), the function we detour.
    FUN_UNITXP              = 0x544090,

    // Lua handler for AttackTarget(). Takes no Lua arguments -- it acts on the
    // current target. On 2.4.3, CGUnit_C::OnRightClick does NOT start auto
    // attack (only the pet-attack command lives there), so this is how the
    // "attack the enemy" half of the interact key has to be done.
    FUN_ATTACK_TARGET       = 0x49E4D0,

    // bool __thiscall(player, target) -- the client's own "is this a peaceful
    // interact target" test. Returns non-zero when both reactions are >= 3,
    // i.e. gossip/vendor/flightmaster. Zero means hostile.
    FUN_IS_PEACEFUL_TARGET  = 0x613820,

    // CGGameUI::SetTarget writes the new target GUID here (at 0x4A67CC).
    GLOBAL_TARGET_GUID      = 0xC6E960,   // uint64 (lo at +0, hi at +4)

    // The unit/object currently under the cursor -- what Lua calls "mouseover".
    // 0x49DA40 (the client's mouseover resolver) reads this first and only
    // falls back to running SecureButton_GetModifiedUnit when it is zero. We
    // read the global directly: calling 0x49DA40 would execute Lua and touch
    // GLOBAL_SCRIPT_STATE, which is exactly what we're trying to avoid.
    GLOBAL_MOUSEOVER_GUID   = 0xC6E950,   // uint64 (lo at +0, hi at +4)

    // FrameScript's "currently executing Lua state". Non-zero while the client
    // is running script, saved/restored around every script call (346 xrefs,
    // nearly all inside the Lua VM at 0x72D000-0x741000).
    //
    // This is TBC's protected-function mechanism. The gate at 0x49DBA0 refuses
    // protected actions whenever this is non-zero -- i.e. whenever the request
    // came from Lua. Our detour runs inside a Lua call by construction, so
    // every guarded action was refused. Vanilla 1.12 has no such check, which
    // is why the original mod never had to deal with it.
    //
    // Do NOT call 0x49DBA0 directly to test this: its deny branch calls
    // 0x498100, which emits the "action blocked by an AddOn" UI message.
    GLOBAL_SCRIPT_STATE     = 0xE1F640,
};

// Virtual method slots on the CGObject_C hierarchy.
// Verified against CGUnit_C (vtable 0x8C32B8), CGPlayer_C (0x8C5580)
// and CGGameObject_C (0x8C4AF0).
enum VTableSlot
{
    // C3Vector* __thiscall GetPosition(C3Vector* out)
    VF_GET_POSITION  = 8,   // +0x20

    // void __thiscall OnRightClick()   -- NOTE: no autoloot arg on 2.4.3
    VF_ON_RIGHTCLICK = 34,  // +0x88
};

// Descriptor (update field) offsets.
//
// *(void**)(object + OBJ_DESCRIPTORS) points at the *unit* field block, i.e.
// the descriptor array already advanced past the 6 shared object fields.
// So: byte offset = (mangos_field_index - 6) * 4.
enum ObjectOffsets
{
    OBJ_DESCRIPTORS         = 0x120,

    // Verified directly in the binary:
    UNIT_FIELD_HEALTH       = 0x040,  // idx 0x16 -- from UnitHealth handler
    UNIT_FIELD_MAXHEALTH    = 0x058,  // idx 0x1C -- from UnitHealthMax handler
    UNIT_FIELD_FLAGS        = 0x0A0,  // idx 0x2E -- from UnitAffectingCombat
    UNIT_DYNAMIC_FLAGS      = 0x278,  // idx 0xA4 -- from UnitIsTapped/ByPlayer

    // Derived from the TBC field table, not directly observed. If pet
    // filtering misbehaves, this is the value to re-check first.
    UNIT_FIELD_SUMMONEDBY   = 0x018,  // idx 0x0C
};

enum UnitFlags
{
    UNIT_FLAG_SKINNABLE   = 0x4000000,
};

enum UnitDynFlags
{
    UNIT_DYNFLAG_LOOTABLE = 0x01,
    UNIT_DYNFLAG_TAPPED   = 0x04,
    UNIT_DYNFLAG_DEAD     = 0x20,
};

typedef struct
{
    float x;
    float y;
    float z;
} C3Vector;

// Callback signature for ClntObjMgrEnumVisibleObjects.
// Return non-zero to continue enumeration, zero to stop early.
typedef int(__cdecl* ENUM_CALLBACK)(uint32_t guidLow, uint32_t guidHigh, void* userdata);

typedef int(__cdecl* LUA_ISSTRING)(void* L, int index);
typedef const char* (__cdecl* LUA_GETTEXT)(void* L, int index, int* len);

// NOTE: __cdecl, not __fastcall. The 1.12 original declared Lua handlers as
// __fastcall; on 8606 the UnitXP handler takes its lua_State* from [ebp+8] and
// ends in a plain `ret`, so the caller cleans up -- that's __cdecl. Getting
// this wrong makes the detour read garbage instead of the Lua state.
typedef int(__cdecl* LUA_CFUNCTION)(void* L);

namespace Game
{
    // Object manager
    bool     IsInWorld();
    uint64_t GetActivePlayerGuid();
    void*    GetObjectPtr(uint64_t guid, uint32_t typeMask);
    void     EnumVisibleObjects(ENUM_CALLBACK callback, void* userdata);

    // Object accessors
    void*    GetDescriptors(void* object);
    C3Vector GetPosition(void* object);

    int      GetUnitHealth(void* unit);
    bool     IsUnitLootable(void* unit);
    bool     IsUnitSkinnable(void* unit);
    uint64_t GetSummonedBy(void* unit);

    // Actions
    void     OnRightClick(void* object);
    void     SetTarget(uint64_t guid);
    void     AttackTarget(void* L);

    // True when the client considers the target peaceful (gossip/vendor/etc).
    bool     IsPeacefulTarget(void* player, void* target);

    uint64_t GetTargetGuid();
    uint64_t GetMouseoverGuid();
}

namespace Lua
{
    bool        IsString(void* L, int index);
    const char* ToString(void* L, int index);
}
