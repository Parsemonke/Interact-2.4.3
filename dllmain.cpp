// dllmain.cpp : Defines the entry point for the DLL application.
// World of Warcraft 2.4.3 (build 8606) port.
//
// The 1.12 original used MinHook. This version has a small built-in detour
// instead, so there is no submodule to fetch and no dependency to build.
// That is viable here because we only ever hook one function, at a known
// address, whose prologue we have already verified.

#include <windows.h>

#include "Game.h"
#include "Log.h"

#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Minimal inline hook
// ---------------------------------------------------------------------------
//
// UnitXP's prologue on 8606 is:
//
//   0x00544090  55           push ebp
//   0x00544091  8B EC        mov  ebp, esp
//   0x00544093  51           push ecx
//   0x00544094  56           push esi
//   0x00544095  8B 75 08     mov  esi, [ebp+8]
//
// That is 8 bytes with no relative branches, so it relocates verbatim into a
// trampoline -- no instruction rewriting needed. We overwrite it with a 5-byte
// JMP plus three NOPs.

namespace
{
    const uintptr_t kHookTarget = Offsets::FUN_UNITXP;
    const size_t    kStolenSize = 8;

    // Guard: if this doesn't match, the client is not the build we analysed and
    // we must not patch it.
    const uint8_t kExpectedPrologue[kStolenSize] =
    {
        0x55, 0x8B, 0xEC, 0x51, 0x56, 0x8B, 0x75, 0x08
    };

    uint8_t  g_originalBytes[kStolenSize] = { 0 };
    uint8_t* g_trampoline = nullptr;
    bool     g_hooked = false;

    bool InstallHook(void* detour)
    {
        uint8_t* target = reinterpret_cast<uint8_t*>(kHookTarget);

        if (memcmp(target, kExpectedPrologue, kStolenSize) != 0)
        {
            return false;
        }

        g_trampoline = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!g_trampoline) return false;

        memcpy(g_trampoline, target, kStolenSize);
        g_trampoline[kStolenSize] = 0xE9;
        *reinterpret_cast<int32_t*>(g_trampoline + kStolenSize + 1) =
            static_cast<int32_t>((target + kStolenSize) - (g_trampoline + kStolenSize + 5));

        memcpy(g_originalBytes, target, kStolenSize);

        DWORD oldProtect = 0;
        if (!VirtualProtect(target, kStolenSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            VirtualFree(g_trampoline, 0, MEM_RELEASE);
            g_trampoline = nullptr;
            return false;
        }

        target[0] = 0xE9;
        *reinterpret_cast<int32_t*>(target + 1) =
            static_cast<int32_t>(static_cast<uint8_t*>(detour) - (target + 5));
        target[5] = 0x90;
        target[6] = 0x90;
        target[7] = 0x90;

        VirtualProtect(target, kStolenSize, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), target, kStolenSize);

        g_hooked = true;
        return true;
    }

    void RemoveHook()
    {
        if (!g_hooked) return;

        uint8_t* target = reinterpret_cast<uint8_t*>(kHookTarget);

        DWORD oldProtect = 0;
        if (VirtualProtect(target, kStolenSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            memcpy(target, g_originalBytes, kStolenSize);
            VirtualProtect(target, kStolenSize, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), target, kStolenSize);
        }

        if (g_trampoline)
        {
            VirtualFree(g_trampoline, 0, MEM_RELEASE);
            g_trampoline = nullptr;
        }

        g_hooked = false;
    }

    // --- diagnostics helpers ---------------------------------------------

    inline int DescInt(void* obj, int off)
    {
        void* d = Game::GetDescriptors(obj);
        if (!d) return 0;
        return *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(d) + off);
    }

    inline uint64_t DescU64(void* obj, int off)
    {
        void* d = Game::GetDescriptors(obj);
        if (!d) return 0;
        return *reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(d) + off);
    }

}

// ---------------------------------------------------------------------------
// Interact logic
// ---------------------------------------------------------------------------

static const float kInteractRange = 5.0f;

struct SearchState
{
    C3Vector playerPos;
    uint64_t playerGuid;
    float    bestDistance;
    void*    best;
    uint64_t bestGuid;
    int      scanned;
    int      inRange;
};

static float Distance3D(const C3Vector& a, const C3Vector& b)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dz = b.z - a.z;

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// Called once per visible object. Return non-zero to keep enumerating.
static int __cdecl OnVisibleObject(uint32_t guidLow, uint32_t guidHigh, void* userdata)
{
    SearchState* state = static_cast<SearchState*>(userdata);
    uint64_t guid = (static_cast<uint64_t>(guidHigh) << 32) | guidLow;

    state->scanned++;

    if (guid == 0 || guid == state->playerGuid) return 1;

    // Rather than reading a type field, let the object manager filter for us.
    void* unit = Game::GetObjectPtr(guid, TypeMask::TYPEMASK_UNIT);
    void* gameObject = unit ? nullptr : Game::GetObjectPtr(guid, TypeMask::TYPEMASK_GAMEOBJECT);
    void* object = unit ? unit : gameObject;

    if (!object) return 1;

    C3Vector position = Game::GetPosition(object);
    float distance = Distance3D(position, state->playerPos);

    if (distance > kInteractRange) return 1;

    state->inRange++;

    // --- log every in-range candidate, before any filtering ---------------
    if (unit)
    {
        LOG("  cand UNIT guid=0x%016llX dist=%.2f hp=%d maxhp=%d flags=0x%08X dyn=0x%08X summonedBy=0x%016llX lootable=%d skinnable=%d",
            guid, distance,
            DescInt(unit, ObjectOffsets::UNIT_FIELD_HEALTH),
            DescInt(unit, ObjectOffsets::UNIT_FIELD_MAXHEALTH),
            DescInt(unit, ObjectOffsets::UNIT_FIELD_FLAGS),
            DescInt(unit, ObjectOffsets::UNIT_DYNAMIC_FLAGS),
            DescU64(unit, ObjectOffsets::UNIT_FIELD_SUMMONEDBY),
            Game::IsUnitLootable(unit) ? 1 : 0,
            Game::IsUnitSkinnable(unit) ? 1 : 0);
    }
    else
    {
        LOG("  cand GOBJ guid=0x%016llX dist=%.2f", guid, distance);
    }

    // Skip anything owned by a player (our own pet, other people's minions).
    if (unit)
    {
        uint64_t summonedBy = Game::GetSummonedBy(unit);
        if (summonedBy != 0 && Game::GetObjectPtr(summonedBy, TypeMask::TYPEMASK_PLAYER) != nullptr)
        {
            LOG("       -> SKIPPED by pet filter (summonedBy resolves to a player)");
            return 1;
        }
    }

    if (distance >= state->bestDistance) return 1;

    if (unit)
    {
        int health = Game::GetUnitHealth(unit);

        if (health > 0)
        {
            state->bestDistance = distance;
            state->best = unit;
            state->bestGuid = guid;
        }
        else if (Game::IsUnitLootable(unit) || Game::IsUnitSkinnable(unit))
        {
            state->bestDistance = distance;
            state->best = unit;
            state->bestGuid = guid;
        }
        else
        {
            LOG("       -> not selected (dead, neither lootable nor skinnable)");
        }
    }
    else
    {
        state->bestDistance = distance;
        state->best = gameObject;
        state->bestGuid = guid;
    }

    return 1;
}

// Acts on a chosen object. Units get targeted first; then the client decides
// what "interact" means -- gossip, loot, skin, melee or ranged attack.
static int PerformInteract(void* object, uint64_t guid, const char* how)
{
    if (!object) return 0;

    bool isUnit = Game::GetObjectPtr(guid, TypeMask::TYPEMASK_UNIT) != nullptr;

    void** vtable = *reinterpret_cast<void***>(object);
    void*  onRightClick = vtable[VTableSlot::VF_ON_RIGHTCLICK];

    LOG("CHOSEN via %s: %s guid=0x%016llX OnRightClick=0x%08X",
        how, isUnit ? "UNIT" : "GOBJ", guid,
        reinterpret_cast<uint32_t>(onRightClick));

    if (isUnit)
    {
        LOG("SetTarget(0x%016llX)", guid);
        Game::SetTarget(guid);
    }

    Game::OnRightClick(object);
    LOG("OnRightClick returned");

    return 1;
}

static int InteractNearest(void* L)
{
    (void)L;   // only needed if the AttackTarget fallback is reinstated

    LOG("=== Interact key pressed ===");

    if (!Game::IsInWorld())
    {
        LOG("not in world - aborting");
        return 0;
    }

    uint64_t playerGuid = Game::GetActivePlayerGuid();
    void* player = Game::GetObjectPtr(playerGuid, TypeMask::TYPEMASK_PLAYER);
    if (!player)
    {
        LOG("could not resolve active player (guid=0x%016llX) - aborting", playerGuid);
        return 0;
    }

    SearchState state;
    state.playerPos    = Game::GetPosition(player);
    state.playerGuid   = playerGuid;
    state.bestDistance = 1000.0f;
    state.best         = nullptr;
    state.bestGuid     = 0;
    state.scanned      = 0;
    state.inRange      = 0;

    LOG("player guid=0x%016llX pos=(%.2f, %.2f, %.2f)",
        playerGuid, state.playerPos.x, state.playerPos.y, state.playerPos.z);

    // --- 1. Mouseover wins, at any range ---------------------------------
    // What you're pointing at is an explicit choice, so it beats proximity and
    // is not distance-limited. The client still refuses out-of-range gossip or
    // gathering on its own; this mainly matters for starting ranged attack.
    // The pet filter is deliberately skipped here -- if you deliberately hover
    // your own pet, that's what you meant.
    uint64_t mouseover = Game::GetMouseoverGuid();
    if (mouseover != 0 && mouseover != playerGuid)
    {
        void* moUnit = Game::GetObjectPtr(mouseover, TypeMask::TYPEMASK_UNIT);
        void* moObj  = moUnit ? moUnit
                              : Game::GetObjectPtr(mouseover, TypeMask::TYPEMASK_GAMEOBJECT);
        if (moObj)
        {
            return PerformInteract(moObj, mouseover, "mouseover");
        }
        LOG("mouseover 0x%016llX did not resolve - falling back to nearest", mouseover);
    }

    // --- 2. Otherwise, nearest within range ------------------------------
    Game::EnumVisibleObjects(&OnVisibleObject, &state);

    LOG("scanned=%d inRange=%d", state.scanned, state.inRange);

    if (!state.best)
    {
        LOG("no candidate chosen - nothing to do");
        return 0;
    }

    LOG("nearest candidate at %.2f yd", state.bestDistance);
    return PerformInteract(state.best, state.bestGuid, "nearest");
}

// The detour. __cdecl to match the original handler.
static int __cdecl detoured_UnitXP(void* L)
{
    // The original validates argument 1 the same way before touching it, so
    // this is safe even when UnitXP is called with no arguments.
    if (Lua::IsString(L, 1))
    {
        const char* arg = Lua::ToString(L, 1);
        if (arg && strcmp(arg, "interact") == 0)
        {
            InteractNearest(L);
            return 0;   // we push no return values
        }
    }

    LUA_CFUNCTION original = reinterpret_cast<LUA_CFUNCTION>(g_trampoline);
    return original(L);
}

// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);

            if (!InstallHook(reinterpret_cast<void*>(&detoured_UnitXP)))
            {
                MessageBoxA(nullptr,
                    "Interact: could not hook UnitXP.\n\n"
                    "The client at 0x544090 does not look like WoW 2.4.3 build 8606, "
                    "or something else has already hooked it.",
                    "Interact", MB_OK | MB_ICONERROR);
                return FALSE;
            }

            LOG("--- Interact.dll attached, hook installed ---");
            break;

        case DLL_PROCESS_DETACH:
            RemoveHook();
            break;
    }

    return TRUE;
}
