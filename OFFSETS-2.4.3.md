# Verified 2.4.3 (8606) offsets

Derived by static analysis of the `Wow.exe` in this install.

```
sha1  a802af90d44c08875fa6949239044afa1a488f92
md5   57c5c03097103e15f9abe2803aebdc3c
build "WoW [Release] Build 8606 (Jul 10 2008 11:43:23)"
ImageBase 0x00400000   .text 0x00401000 (0x48815B)
```

This is a **stock, unmodified 8606 client** — no private-server patching, no added
imports. That's what makes these values portable to other 8606 installs.

Everything below marked **verified** was read directly out of this binary. The one
value marked *derived* was not directly observed.

---

## Functions

| Constant | Signature | Address | How |
|---|---|---|---|
| `FUN_OBJECT_POINTER` | `void* __cdecl ClntObjMgrObjectPtr(uint64 guid, uint32 typeMask, const char* file, int line)` | `0x46B610` | verified — called from the `TargetUnit` Lua handler with `".\GameUI.cpp"` |
| `FUN_ENUM_VISIBLE` | `int __cdecl ClntObjMgrEnumVisibleObjects(cb, void* userdata)` | `0x46B3F0` | verified — full loop disassembled |
| `FUN_ACTIVE_PLAYER_GUID` | `uint64 __cdecl()` (edx:eax) | `0x469DD0` | verified — reads `objmgr + 0xC0/0xC4` |
| `FUN_GET_CUR_MGR` | `void* __cdecl()` | `0x469D70` | verified — NULL when not in world |
| `FUN_SET_TARGET` | `void __cdecl(uint64 guid)` | `0x4A6690` | verified — tail of the `TargetUnit` handler |
| `FUN_LUA_ISSTRING` | `int __cdecl(lua_State*, int idx)` | `0x72DE70` | verified — arg guard in every `Unit*` handler |
| `FUN_LUA_GETTEXT` | `const char* __cdecl(lua_State*, int idx, int* len)` | `0x72DFF0` | verified — `FrameScript_GetText` |
| `FUN_UNITXP` | Lua handler for `UnitXP()` | `0x544090` | verified — registration table entry at `0xB9F060` |

Bonus, not used by the port but resolved along the way:

| What | Address |
|---|---|
| `FrameScript_PushNumber(L, double)` | `0x72E1A0` |
| `FrameScript_PushNil(L)` | `0x72E180` |
| `FrameScript_Usage/Error(L, const char*)` | `0x72F5C0` |
| `GetUnitFromName(const char*)` — resolves `"target"`, `"mouseover"`, … | `0x541EA0` |
| Lua function registration table start | `0xB9EF60` (2169 `{name, fn}` pairs found) |

---

## Object manager

**The object manager is thread-local on 2.4.3**, unlike 1.12's flat pointer at
`0xB41414`:

```
slot   = *(uint32*)0xE2563C          // TLS slot index
objmgr = ((void**)__readfsdword(0x2C))[slot] + 8
```

Consequence: all of this **must run on the main game thread**. Inside a Lua call
(which is where the detour fires) that's satisfied. From a worker thread you'd read a
different — probably null — object manager.

| Field | Offset | Note |
|---|---|---|
| object list head | `objmgr + 0xAC` | same as 1.12 |
| active player GUID | `objmgr + 0xC0` | same as 1.12 |
| **link offset** | `objmgr + 0xA4` | **read at runtime** — not a constant `0x3C` |
| object GUID | `object + 0x30` | low dword; `+0x34` high |

The port doesn't touch any of this directly — it calls `ClntObjMgrEnumVisibleObjects`,
which handles the TLS lookup and the dynamic link offset itself. That's the single
biggest robustness win over the 1.12 approach.

Enumeration contract, from the disassembly:

```c
int __cdecl callback(uint32 guidLow, uint32 guidHigh, void* userdata);
// return non-zero to continue, zero to stop early
```

---

## Object layout

`*(void**)(object + 0x120)` is the descriptor block, **already advanced past the six
shared object fields**. So the conversion from an emulator's `UpdateFields.h` index is:

```
byteOffset = (index - 6) * 4
```

| Field | Offset from `+0x120` | Index | Evidence |
|---|---|---|---|
| `UNIT_FIELD_HEALTH` | `0x040` | `0x16` | **verified** — `UnitHealth` handler |
| `UNIT_FIELD_MAXHEALTH` | `0x058` | `0x1C` | **verified** — `UnitHealthMax` handler |
| `UNIT_FIELD_FLAGS` | `0x0A0` | `0x2E` | **verified** — `UnitAffectingCombat` tests bit `0x80000` |
| `UNIT_DYNAMIC_FLAGS` | `0x278` | `0xA4` | **verified** — `UnitIsTapped` (`&4`) and `UnitIsTappedByPlayer` (`&8`) |
| `UNIT_FIELD_SUMMONEDBY` | `0x018` | `0x0C` | *derived* — see caveat below |

Flag bits confirmed against `CGUnit_C::OnRightClick`, which tests dynamic-flag bit 5
(`UNIT_DYNFLAG_DEAD`) on the health<=0 path — consistent with the standard TBC
dynamic-flag set, so `LOOTABLE = 0x1` holds. `UNIT_FLAG_SKINNABLE = 0x4000000` is
unchanged from 1.12.

Two indices are worth noting because they happen to match 1.12 exactly (`HEALTH`
`0x16`, `FLAGS` `0x2E`) while `UNIT_DYNAMIC_FLAGS` moved from `0x8F` to `0xA4` — TBC
inserted fields in between. Copying the 1.12 dynamic-flags offset would have silently
read the wrong field rather than crashing, which is the nastiest failure mode here.

---

## Virtual methods

| Slot | Offset | Method | CGUnit_C | CGGameObject_C |
|---|---|---|---|---|
| 8 | `+0x20` | `C3Vector* __thiscall GetPosition(C3Vector* out)` | `0x5EC940` | `0x6014A0` |
| 34 | `+0x88` | `void __thiscall OnRightClick()` | `0x619E00` | `0x600960` |

Vtable bases, pinned from the constructors that write them:

| Class | Vtable | Constructor writes at |
|---|---|---|
| `CGUnit_C` | `0x8C32B8` | `0x5EC5F0`, `0x5ECA14` |
| `CGPlayer_C` | `0x8C5580` | `0x61929C`, `0x61FAF9` |
| `CGGameObject_C` | `0x8C4AF0` | `0x602C4B` |

**`OnRightClick` takes no `autoloot` argument on 2.4.3.** The 1.12 code passed one
(`__thiscall(this, int autoloot)`); the 8606 functions both end in a bare `ret`, so
they're `__thiscall(this)` and the client reads the autoloot setting itself. Passing an
extra argument would corrupt the stack.

`CGUnit_C::OnRightClick` was cross-checked behaviourally: it fetches the active player
via `0x469DD0` → `ClntObjMgrObjectPtr(guid, 0x10, ...)`, bails if player health is 0,
then branches between the attack path and the loot/skin path on target health and
dynamic-flag bit 5. `CGGameObject_C::OnRightClick` does the same player lookup, clears
the cursor, then tail-jumps to the gameobject-use path at `0x5FF530`.

Calling these through the vtable rather than by address means the port doesn't care
which class it got — one call site handles mobs, corpses, herbs, veins and chests.

---

## Type masks

`ClntObjMgrObjectPtr` takes a **bitmask**, not the 1.12 type enum, and returns NULL if
the object doesn't match. The port uses this as its type test instead of reading a type
field:

```
OBJECT 0x01  ITEM 0x02  CONTAINER 0x04  UNIT 0x08
PLAYER 0x10  GAMEOBJECT 0x20  DYNAMICOBJECT 0x40  CORPSE 0x80
```

Note `PLAYER` does not imply `UNIT` here — a player object queried with mask `0x08`
returns NULL. The port relies on that to skip other players cheaply.

---

## Protected actions — the big 1.12 → 2.4.3 difference

This is the part that has no equivalent in vanilla and cost the most time, so
it's worth stating plainly.

TBC gates certain actions on **`0xE1F640`**, FrameScript's *currently executing
Lua state*. It is non-zero while the client runs script, and saved/restored
around every script call (346 xrefs, nearly all inside the Lua VM at
`0x72D000`–`0x741000`).

The gate is `0x49DBA0(category)`:

```
if (*(uint32*)0xE1F640 == 0) return 1;   // native caller -> allowed
switch (category) { ... case 4: report(); return 0; }   // Lua caller -> denied
```

The interact key reaches the DLL through `Bindings.xml` → `UnitXP("interact")`,
so our detour **always** runs with that flag set. Every guarded action was
therefore refused, silently.

Which paths are guarded, traced from the binary:

| Action | Function | Guarded |
|---|---|---|
| Loot | `0x5E2460` | no |
| Gossip / quest / taxi | `0x5E6780` | no |
| GameObject use (nodes, chests, doors) | `0x600960` | no |
| **Attack** | `0x5EA540` | **yes** |
| **Skin** | `0x5DF630` (via `0x6FBC10`) | **yes** |
| **Targeting** | `0x4A6690` → `0x49DBA0(4)` | **yes** |

That table predicted the observed behaviour exactly — loot, gossip and mining
nodes worked from the first build; attack and skinning never did. That match is
the evidence the diagnosis is right.

The fix is `ScriptContextBypass` in `Game.cpp`: save `0xE1F640`, zero it, make
the call, restore. Applied to `SetTarget`, `OnRightClick` and `AttackTarget`.

**Never call `0x49DBA0` just to inspect the gate.** Its deny branch calls
`0x498100`, which formats a message into the Lua error channel — that's TBC's
"action blocked by an AddOn" reporter. A diagnostic that tested the gate this
way produced a UI message on every single keypress, and the message was coming
from the diagnostic, not from the interaction.

## Attack is not in OnRightClick

A second structural difference. On 1.12, `CGUnit_C::OnRightClick` starts auto
attack. On 8606 it does not — the only attack inside it is the *pet* attack
command. `0x5E6780`, which looks like the attack branch, actually reaches
`CMSG_GOSSIP_HELLO`.

So the port targets the unit and then calls the client's own `AttackTarget`
Lua handler (`0x49E4D0`), choosing between that and `OnRightClick` using the
client's own reaction test (`0x613820`).

## The one value that needed confirming — now confirmed

`UNIT_FIELD_SUMMONEDBY` at `+0x18` was the only offset that couldn't be observed
directly in the binary; no 8606 Lua handler reads it in a way that pins the index.
It was derived from the TBC field table and matched 1.12 (index `0x0C`).

**Confirmed correct in play.** A runtime log showed the player's own pet with
`summonedBy=0x00000000000014C2`, exactly matching the active player GUID, and the
pet filter skipping it on every keypress. No open risks remain in the offset table.
