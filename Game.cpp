#include "Game.h"

//
// 2.4.3 (8606) implementations.
//
// Two deliberate design changes versus the original 1.12 code:
//
//  1. Object enumeration goes through the client's own
//     ClntObjMgrEnumVisibleObjects instead of walking the linked list by hand.
//     On 2.4.3 the "next object" link offset is not a constant -- the client
//     reads it from (objmgr + 0xA4) at runtime -- so hand-walking with a
//     hardcoded 0x3C is wrong here.
//
//  2. Positions and the right-click action are called through the object's
//     vtable rather than fixed addresses. Both differ between CGUnit_C and
//     CGGameObject_C, and the virtual dispatch picks the right one for free.
//

namespace
{
    typedef void* (__cdecl* FN_OBJECT_PTR)(uint64_t guid, uint32_t typeMask, const char* file, int line);
    typedef int   (__cdecl* FN_ENUM_VISIBLE)(ENUM_CALLBACK callback, void* userdata);
    typedef uint64_t(__cdecl* FN_ACTIVE_PLAYER_GUID)();
    typedef void* (__cdecl* FN_GET_CUR_MGR)();
    typedef void  (__cdecl* FN_SET_TARGET)(uint64_t guid);

    typedef C3Vector* (__thiscall* FN_GET_POSITION)(void* self, C3Vector* out);
    typedef void      (__thiscall* FN_ON_RIGHTCLICK)(void* self);

    const FN_OBJECT_PTR          p_ObjectPtr        = reinterpret_cast<FN_OBJECT_PTR>(Offsets::FUN_OBJECT_POINTER);
    const FN_ENUM_VISIBLE        p_EnumVisible      = reinterpret_cast<FN_ENUM_VISIBLE>(Offsets::FUN_ENUM_VISIBLE);
    const FN_ACTIVE_PLAYER_GUID  p_ActivePlayerGuid = reinterpret_cast<FN_ACTIVE_PLAYER_GUID>(Offsets::FUN_ACTIVE_PLAYER_GUID);
    const FN_GET_CUR_MGR         p_GetCurMgr        = reinterpret_cast<FN_GET_CUR_MGR>(Offsets::FUN_GET_CUR_MGR);
    const FN_SET_TARGET          p_SetTarget        = reinterpret_cast<FN_SET_TARGET>(Offsets::FUN_SET_TARGET);

    const LUA_ISSTRING p_lua_isstring = reinterpret_cast<LUA_ISSTRING>(Offsets::FUN_LUA_ISSTRING);
    const LUA_GETTEXT  p_lua_gettext  = reinterpret_cast<LUA_GETTEXT>(Offsets::FUN_LUA_GETTEXT);

    // The client passes __FILE__/__LINE__ into ClntObjMgrObjectPtr for its
    // internal asserts. Anything stable works.
    const char* kFile = "Interact.cpp";
    const int   kLine = 1;

    // TBC gates certain actions on FrameScript's "currently executing Lua"
    // global: 0x49DBA0 refuses them whenever it is non-zero. Our detour runs
    // inside a Lua call by construction, so every guarded action was refused.
    //
    // Which paths are guarded (traced from the 8606 binary):
    //     loot        0x5E2460   unguarded
    //     gossip      0x5E6780   unguarded
    //     gameobject  0x600960   unguarded
    //     attack      0x5EA540   GUARDED
    //     skin        0x5DF630   GUARDED (via 0x6FBC10)
    //
    // That is exactly the working/broken split we observed, which is the
    // evidence this is the real cause and not a coincidence.
    //
    // Scope is kept to a single call: while the flag is cleared, any client
    // code that runs believes there is no active Lua state.
    struct ScriptContextBypass
    {
        uint32_t saved;

        ScriptContextBypass()
        {
            uint32_t* slot = reinterpret_cast<uint32_t*>(Offsets::GLOBAL_SCRIPT_STATE);
            saved = *slot;
            *slot = 0;
        }

        ~ScriptContextBypass()
        {
            *reinterpret_cast<uint32_t*>(Offsets::GLOBAL_SCRIPT_STATE) = saved;
        }
    };

    inline void* VFunc(void* object, int slot)
    {
        void** vtable = *reinterpret_cast<void***>(object);
        return vtable[slot];
    }

    inline int DescriptorInt(void* object, int fieldOffset)
    {
        void* descriptors = Game::GetDescriptors(object);
        if (!descriptors) return 0;
        return *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(descriptors) + fieldOffset);
    }
}

namespace Game
{
    bool IsInWorld()
    {
        return p_GetCurMgr() != nullptr;
    }

    uint64_t GetActivePlayerGuid()
    {
        return p_ActivePlayerGuid();
    }

    void* GetObjectPtr(uint64_t guid, uint32_t typeMask)
    {
        if (guid == 0) return nullptr;
        return p_ObjectPtr(guid, typeMask, kFile, kLine);
    }

    void EnumVisibleObjects(ENUM_CALLBACK callback, void* userdata)
    {
        p_EnumVisible(callback, userdata);
    }

    void* GetDescriptors(void* object)
    {
        if (!object) return nullptr;
        return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(object) + ObjectOffsets::OBJ_DESCRIPTORS);
    }

    C3Vector GetPosition(void* object)
    {
        C3Vector out = { 0.0f, 0.0f, 0.0f };
        if (!object) return out;

        FN_GET_POSITION getPosition =
            reinterpret_cast<FN_GET_POSITION>(VFunc(object, VTableSlot::VF_GET_POSITION));

        C3Vector* result = getPosition(object, &out);
        return result ? *result : out;
    }

    int GetUnitHealth(void* unit)
    {
        return DescriptorInt(unit, ObjectOffsets::UNIT_FIELD_HEALTH);
    }

    bool IsUnitLootable(void* unit)
    {
        int flags = DescriptorInt(unit, ObjectOffsets::UNIT_DYNAMIC_FLAGS);
        return (flags & UnitDynFlags::UNIT_DYNFLAG_LOOTABLE) != 0;
    }

    bool IsUnitSkinnable(void* unit)
    {
        int flags = DescriptorInt(unit, ObjectOffsets::UNIT_FIELD_FLAGS);
        return (flags & UnitFlags::UNIT_FLAG_SKINNABLE) == UnitFlags::UNIT_FLAG_SKINNABLE;
    }

    uint64_t GetSummonedBy(void* unit)
    {
        void* descriptors = GetDescriptors(unit);
        if (!descriptors) return 0;
        return *reinterpret_cast<uint64_t*>(
            reinterpret_cast<uint8_t*>(descriptors) + ObjectOffsets::UNIT_FIELD_SUMMONEDBY);
    }

    void OnRightClick(void* object)
    {
        if (!object) return;

        FN_ON_RIGHTCLICK onRightClick =
            reinterpret_cast<FN_ON_RIGHTCLICK>(VFunc(object, VTableSlot::VF_ON_RIGHTCLICK));

        // Needed for the skin branch, which is guarded. Loot, gossip and the
        // gameobject path are unguarded and unaffected by this.
        ScriptContextBypass bypass;
        onRightClick(object);
    }

    void SetTarget(uint64_t guid)
    {
        ScriptContextBypass bypass;
        p_SetTarget(guid);
    }

    void AttackTarget(void* L)
    {
        typedef int(__cdecl* FN)(void* L);

        // AttackTarget reaches the guarded attack path at 0x5EA540. Note L is
        // passed explicitly, so clearing the *global* state does not deprive
        // the handler of its lua_State.
        ScriptContextBypass bypass;
        reinterpret_cast<FN>(Offsets::FUN_ATTACK_TARGET)(L);
    }

    bool IsPeacefulTarget(void* player, void* target)
    {
        typedef char(__thiscall* FN)(void* self, void* target);
        return reinterpret_cast<FN>(Offsets::FUN_IS_PEACEFUL_TARGET)(player, target) != 0;
    }

    uint64_t GetTargetGuid()
    {
        return *reinterpret_cast<uint64_t*>(Offsets::GLOBAL_TARGET_GUID);
    }

    uint64_t GetMouseoverGuid()
    {
        return *reinterpret_cast<uint64_t*>(Offsets::GLOBAL_MOUSEOVER_GUID);
    }
}

namespace Lua
{
    bool IsString(void* L, int index)
    {
        return p_lua_isstring(L, index) != 0;
    }

    const char* ToString(void* L, int index)
    {
        return p_lua_gettext(L, index, nullptr);
    }
}
