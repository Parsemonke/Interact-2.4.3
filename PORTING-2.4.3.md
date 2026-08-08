# Interact — 2.4.3 (8606) port status

## Where this stands

**Working in play.** Looting, NPC gossip/quests/flightmasters, gameobjects (mining and
herb nodes, chests, doors), attacking and skinning all confirmed working on a live
client.

- `Interact.toc` bumped to `## Interface: 20400`
- `Game.h` / `Game.cpp` / `dllmain.cpp` rewritten against 8606
- `Interact.lua` and `Bindings.xml` unchanged (they were already portable)
- Diagnostic logging compiled out (`INTERACT_LOGGING 0` in `Log.h`)

## Target selection

1. **Mouseover, at any range.** Whatever is under the cursor
   (`0xC6E950`) wins — pointing at something is an explicit choice, so it beats
   proximity. Not distance-limited, which is what lets a hunter start ranged
   attack on a distant mob. The client still refuses out-of-range gossip and
   gathering by itself.
2. **Otherwise nearest within 5 yards**, the upstream 1.12 behaviour.

The pet filter applies only to the nearest-object scan. If you deliberately
hover your own pet, that's taken as intended.

Both routes then do the same thing: target it if it's a unit, then call
`OnRightClick` and let the client decide what interacting means. There is no
per-action special-casing in the mod at all.

Every address is verified against the `Wow.exe` in this install — see
`OFFSETS-2.4.3.md` for the value-by-value evidence, including the two structural
differences from 1.12 that took the longest to find.

---

## What actually changed, and why

The 1.12 original hardcoded a flat object-manager pointer, a fixed `0x3C` link offset,
fixed position offsets, and fixed right-click addresses. Three of those assumptions are
wrong on 2.4.3 in ways that don't announce themselves:

**1. The object manager is thread-local.** 1.12 reads `*(uint32*)0xB41414`. 2.4.3 reads
a TLS slot (`fs:[0x2C]` indexed by `*(uint32*)0xE2563C`, then `+8`). Everything must run
on the main game thread — which it does, since the detour fires inside a Lua call.

**2. The "next object" link offset is not a constant.** The client reads it from
`objmgr + 0xA4` at runtime. Hand-walking with `0x3C` would work until it didn't.

Both are sidestepped: the port calls the client's own
`ClntObjMgrEnumVisibleObjects` (`0x46B3F0`) and lets it do the walking.

**3. `OnRightClick` lost its `autoloot` parameter.** On 1.12 it's
`__thiscall(this, int autoloot)`; on 8606 both the unit and gameobject versions end in a
bare `ret`, so they're `__thiscall(this)`. Passing the old extra argument would corrupt
the stack. The client reads the autoloot setting itself now.

**4. TBC protects actions; vanilla doesn't.** The client refuses targeting, attacking
and skinning when the request comes from Lua — and the interact key reaches the DLL
through `Bindings.xml`, so it always does. This has no 1.12 equivalent at all. See
the "Protected actions" section of `OFFSETS-2.4.3.md`; it's handled by
`ScriptContextBypass` in `Game.cpp`.

**5. Attack isn't in `OnRightClick` on 2.4.3.** Only the pet-attack command lives
there. The port targets the unit, then calls the client's own `AttackTarget` handler.

Two further changes are improvements rather than necessities:

- **Positions and right-click go through the vtable** (slots 8 and 34) instead of fixed
  addresses. One call site now handles mobs, corpses, herbs, veins and chests, because
  virtual dispatch picks the right implementation.
- **Type detection uses the object manager's own type mask.** `ClntObjMgrObjectPtr`
  takes a mask and returns NULL on mismatch, so the port asks "is this a unit?" instead
  of reading a type field and comparing an enum.

Also worth knowing: `UNIT_DYNAMIC_FLAGS` moved from index `0x8F` (1.12) to `0xA4` (TBC).
Copying the old offset would have read a valid-but-wrong field — no crash, just a key
that quietly refuses to loot things. That one was worth catching.

---

## The loader

This install already runs an injected, MinHook-based DLL: `wow_optimize.dll` has no
exports and is not in `Wow.exe`'s import table, so something is injecting it — almost
certainly `WoWMe.exe`, the .NET launcher your `SoC.lnk` points at.

So an injection path **already exists here**. The practical options, in order of effort:

1. Find out how `WoWMe.exe` picks up `wow_optimize.dll` and add `Interact.dll` the same
   way. Cheapest if it reads a list; it's a .NET assembly, so it decompiles readily.
2. Any generic `CreateRemoteThread` + `LoadLibraryA` injector, run after launch.
3. Add `Interact.dll` to `Wow.exe`'s import table with CFF Explorer — automatic, but
   breaks on any client repair/update.

VanillaFixes is not usable: it's a 1.12.1-only launcher. None of its timing fixes are
needed here anyway, since `wow_optimize.dll` is already doing RDTSC/QPC work.

One caution: an injected DLL is detectable if the server runs Warden. Most 2.4.3
servers don't. Worth confirming for Season of the Crusade before you rely on it.

---

## Build

Unchanged from upstream — Visual Studio, 32-bit, with the `minhook` submodule:

```
git submodule update --init
```

Target **x86**, not x64. The client is 32-bit and the DLL must match.

---

## Bring-up order

The read-only steps are safe to iterate on; only the last one can crash the client.

1. Load the addon with no DLL. The `Interact` keybind should appear at the bottom of the
   keybindings list. Validates the TOC bump for free.
2. Get `Interact.dll` injecting at all — stub `DllMain` that writes a log file.
3. Confirm the `UnitXP` detour fires: log from `detoured_UnitXP` when arg 1 is
   `"interact"`, and check that normal `UnitXP("player")` calls still return correctly.
4. Log the enumeration — GUID, distance, and whether each object resolved as unit or
   gameobject. This is where you'll know the port is real.
5. Log health / lootable / skinnable for a nearby corpse and confirm they match what the
   game shows. This is the check that catches a wrong descriptor offset, which otherwise
   fails silently.
6. Only then enable `SetTarget` + `OnRightClick`.

Step 5 is the one people skip and regret.

---

## Known soft spot

`UNIT_FIELD_SUMMONEDBY` (`+0x18` from the descriptor block) is the single offset that
couldn't be confirmed directly from the binary — no 8606 Lua handler reads it in a way
that pins the index. It's derived from the TBC field table and matches what the 1.12
version used.

It only drives the pet filter, and a wrong value fails visibly and harmlessly: the key
starts targeting your own pet, or stops skipping other players' minions. If that
happens, that's the value to correct.
