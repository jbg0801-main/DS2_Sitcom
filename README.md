# DS2 Sitcom

Sitcom canned-audience overlay for **Dark Souls II** and **Dark Souls II: Scholar of the First Sin** (PC).

Plays local WAV clips when:

- the player takes damage or dies (laughter; random among `laugh*.wav`)
- a boss fight starts (`ActiveBossBattleId` rising edge — fog / named bar) (cheering)
- that boss’s defeated event flag flips (applause)
- the current map / area id changes after a load (scene wipe — proxy for the area title card)
- an item is granted via the game’s ItemGive path (**ooh**)
- fail states: empty Estus, locked/invalid use, no spell, ladder fall (CurrentAnim when readable)

Vanilla DS2 is **32-bit DirectX 9**. SotFS is **64-bit DirectX 11**. Those need **different** `dinput8.dll` files (the zip names below). Dropping the wrong bitness DLL will not load.

## Install

**Scholar of the First Sin** (Steam app 335300) — use `DS2SotFS_Sitcom.zip`:

1. Copy `dinput8.dll` and the `sitcom/` folder into  
   `.../Dark Souls II Scholar of the First Sin/Game/` (next to `DarkSoulsII.exe`).
2. Launch the game normally.

**Vanilla Dark Souls II** (Steam app 236430, including the DX9 DLC bundle) — use `DS2_Sitcom.zip`:

1. Copy `dinput8.dll` and `sitcom/` into  
   `.../Dark Souls II/Game/` (next to `DarkSoulsII.exe`).

**Linux / Proton** launch options:

```text
WINEDLLOVERRIDES="dinput8.dll=n,b" %command%
```

Put WAVs in `sitcom/sounds/` using the names below. Tweak `sitcom/config.ini`. Set `log=true` to write `sitcom/sitcom.log`.

PlaceName wipe follows `FeSceneMapName` text ids (see `tools/POINTERS.md`). Set `trace_area_title=true` only to diagnose.

**Offline / single-player recommended.** Injected DLLs can interact badly with DS2’s online checks (softbans are poorly documented).

### Chain-loading other `dinput8` mods

Sitcom owns `dinput8.dll` and can load other mods that also need that filename:

1. Install Sitcom as usual (`dinput8.dll` + `sitcom/`).
2. Rename the other mod’s `dinput8.dll` (e.g. to `bbj.dll` or `nologo.dll`) and keep it in the same `Game/` folder.
3. List it in either place:
   - `sitcom/config.ini` → `[chainload] dlls=bbj.dll,nologo.dll`
   - or `sitcom/chainload.txt` (one DLL per line)

Every listed DLL is `LoadLibrary`’d (so its `DllMain` runs). If a listed DLL exports `DirectInput8Create`, Sitcom forwards input to the **first** one that does; otherwise it uses the system `dinput8.dll`. Check `sitcom/sitcom.log` for `proxy: chainloaded …` lines.

## Sound files

Preferred format: **WAV, PCM signed 16-bit, 44100 Hz** (48000 OK). Stereo is fine.

The loader picks randomly among `name.wav` and `name_XX.wav` / `name-XX.wav` for each
category when multiple files are present (avoids immediate repeats).

| Prefix / name | Event |
| --- | --- |
| `laugh.wav` / `laugh_XX.wav` | Hit / death / fail laughs |
| `cheer.wav` / `cheer_XX.wav` | Boss fight start (`ActiveBossBattleId`) |
| `applause.wav` / `applause_XX.wav` | Boss defeated flag |
| `ooh.wav` / `ooh_XX.wav` | Item get |
| `scene_wipe.wav` / `scene_wipe_XX.wav` | Area / map change |

## Build (Linux → Windows DLL)

Requires MinGW-w64 (`x86_64-w64-mingw32-g++` and `i686-w64-mingw32-g++`) and CMake.

```bash
# SotFS (64-bit)
cmake -S . -B build/sotfs -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
cmake --build build/sotfs -j

# Vanilla (32-bit)
cmake -S . -B build/vanilla -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686-toolchain.cmake
cmake --build build/vanilla -j
```

Or: `bash scripts/build-podman.sh` (builds both + packs `dist/DS2SotFS_Sitcom.zip` and `dist/DS2_Sitcom.zip`).

## How it works

`dinput8.dll` proxies `DirectInput8Create` to the system DLL and starts a worker thread that:

1. Detects SotFS vs vanilla from `DarkSoulsII.exe` bitness / module size
2. Resolves GameManagerImp (BaseA), PlayerCtrl, LoadState, EventManager via AOB scans
3. Polls ~20 Hz for HP, loading, event flags, `ActiveBossBattleId`, and ItemGive
4. Plays WAVs through **winmm `PlaySound`** with PCM gain  
   (`config volume` × in-game **Sound Effect** / FMOD when captured)
5. Draws a title-menu credit (`Sitcom mod by jbg0801 2026` plus dedication)

See [tools/POINTERS.md](tools/POINTERS.md) for pointer notes.
