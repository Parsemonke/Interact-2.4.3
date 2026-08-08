# Building Interact.dll

You need a C++ compiler. That's the only prerequisite — this version has no
submodules and no library dependencies, so once the compiler is installed the
build is one double-click.

## 1. Install the compiler (once, ~10 minutes)

Download **Visual Studio Build Tools 2022** — free, and much smaller than full
Visual Studio since it has no IDE:

<https://aka.ms/vs/17/release/vs_BuildTools.exe>

Run it. On the **Workloads** tab, tick:

> ☑ **Desktop development with C++**

Leave the default optional components as they are and click Install. That's
roughly 2–3 GB.

You do not need a Microsoft account, and you don't need to open Visual Studio
afterwards.

## 2. Build

Double-click **`build.bat`** in this folder.

It finds your Visual Studio install automatically, sets up a **32-bit** build
environment, and compiles. If it can find your game folder it copies
`Interact.dll` there; otherwise it leaves the DLL here and tells you to copy it
next to `WoW.exe` yourself.

Expected output:

```
Compiling Interact.dll (x86) ...
  Built OK and copied to:
    C:\...\your game folder
```

## 3. Run

Use **`Launch-Interact.bat`** in the game folder. It starts the game as normal,
then injects the DLL once the client is up.

Then bind a key: **Esc → Key Bindings → Interact** (near the bottom of the list).

---

## If it goes wrong

**"Could not find vswhere.exe"** — Build Tools isn't installed, or the install
didn't finish. Re-run the installer.

**"Visual Studio is installed, but the C++ tools are not"** — you installed
Build Tools but didn't tick *Desktop development with C++*. Re-run the installer
and tick it.

**Compiler errors** — send me the output. These are almost always a typo I
introduced, not something on your end.

**Game launches but the keybind does nothing** — the DLL didn't load. Run
`Launch-Interact.bat` from a terminal so you can read the injector's output
before the window closes.

**A message box saying "could not hook UnitXP"** — the DLL loaded but the client
wasn't what we expect at `0x544090`. That guard exists so a wrong client gets a
clear error instead of a crash. It should not happen on your install, since the
offsets were derived from your exact `Wow.exe`.

**Windows Defender objects to the injector** — process injection is a shape AV
heuristics dislike. It's the PowerShell script, not the DLL. Nothing here phones
home; all of it is readable source you can inspect.

---

## Why there's no Visual Studio project file

The upstream 1.12 repo depended on MinHook as a git submodule, and the `minhook`
folder in this copy is empty — the submodule was never fetched. Rather than have
you deal with git submodules, the DLL now includes its own ~60-line detour.

That's a reasonable trade here because the mod hooks exactly one function, at a
known fixed address, whose first 8 bytes we verified contain no relative
branches — so they relocate into a trampoline verbatim. MinHook's value is
handling the general case; we don't have the general case.

The hook also checks those 8 bytes match before patching, and refuses to install
if they don't.
