# Interact — TBC 2.4.3 (build 8606)

An interact key for The Burning Crusade. Bind one key and use it to attack,
talk to NPCs, loot, skin, mine, herb, open chests and doors — whatever you're
pointing at, or whatever is closest.

A port of [luskanek's Interact](https://github.com/luskanek/Interact), which
targets vanilla 1.12. This version is for **2.4.3 client build 8606** and does
not need VanillaFixes.

---

## Install

1. Copy **everything inside `INSTALL/`** into your WoW folder — the one with
   `WoW.exe` in it. You should end up with:

   ```
   <WoW folder>/Interact.dll
   <WoW folder>/Launch-Interact.bat
   <WoW folder>/Inject-Interact.ps1
   <WoW folder>/Interface/AddOns/Interact/
   ```

2. Launch the game with **`Launch-Interact.bat`** instead of your usual
   launcher. It starts the game exactly as before, then loads the DLL once the
   client is up.

3. In game: **Esc → Key Bindings → Interact** (near the bottom of the list).
   Bind whatever key you like.

Nothing on disk is modified — no patched `WoW.exe`, no changed game files. To
uninstall, delete the four items above.

> **Using a custom launcher already?**
> `Launch-Interact.bat` runs `WoWMe.exe` by default. If your server uses a
> different launcher, open the .bat in a text editor and change that one line.

---

## Compatibility

**This build only works on client 2.4.3, build 8606.** Every address in it was
read out of that specific binary. On any other build the DLL will refuse to
load and show a message box rather than doing anything dangerous.

To check yours: the version is in the bottom-left corner of the login screen.

It has been tested against a stock, unmodified `WoW.exe`. If your server ships
a patched client, the offsets may not line up — the same guard applies, so
you'll get a clear error rather than a crash.

---

## What it does

Pressing the key picks a target in this order:

1. **Whatever is under your cursor**, at any range. Pointing at something is an
   explicit choice, so it wins over anything nearby. Not distance-limited,
   which is what lets a hunter open on a distant mob.
2. **Otherwise the closest valid thing within 5 yards.** Your own pet and other
   players' minions are skipped so they don't get in the way.

Then it targets it and hands off to the client, which decides what interacting
means — attack, gossip, loot, skin, gather, open. There's no per-action logic
in the mod, so it behaves exactly like a right-click, including melee vs ranged.

---

## Troubleshooting

**Keybind doesn't appear** — the addon half isn't installed. Check
`Interface/AddOns/Interact/` exists and contains three files, and that the
addon is enabled at the character screen.

**Keybind does nothing** — the DLL isn't loading. Run `Launch-Interact.bat`
from a terminal so you can read the injector output before the window closes.

**"could not hook UnitXP"** — wrong client build. See Compatibility above.

**Antivirus complains** — the launcher injects a DLL, which is a shape AV
heuristics dislike. It's the PowerShell script, not the DLL. Everything here is
readable source you can inspect, and nothing contacts the network.

**Something else** — set `INTERACT_LOGGING` to `1` in `source/Log.h`, rebuild,
and it writes `Logs/Interact.log` with a full breakdown of every keypress.

---

## Building it yourself

See `source/BUILDING.md`. Short version: install the free Visual Studio Build
Tools with the C++ workload, then double-click `build.bat`. No submodules, no
dependencies — the DLL includes its own hook.

`source/OFFSETS-2.4.3.md` documents every address with how it was derived, and
`source/PORTING-2.4.3.md` covers the structural differences between 1.12 and
2.4.3 that made this more than an offset swap.

---

## Credits

Original mod by **luskanek**. Original credits to **allfoxwy** and **Zz9uk3**,
whose repositories the vanilla version drew on.

MIT licensed — see `LICENSE`.
