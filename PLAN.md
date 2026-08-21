# DS2 Sitcom — Implementation Plan

Sitcom canned-audience mod for **Dark Souls II** and **Dark Souls II: Scholar of the First Sin** (PC).

## Goal

Play overlay SFX on:

- player hit / death (laugh)
- boss loaded while undefeated (cheer)
- boss defeat flag (applause)
- map / area id change (scene wipe)
- ItemGive (ooh)
- fail laughs (empty Estus, locked door, no spell, ladder fall)

Non-goals for this port: DSR-only gags (Sif/Pinwheel/BoC/Smough), online safety guarantees, ModEngine, editing game soundbanks.

## Approach

`dinput8.dll` proxy + background poller + local WAV player. Two builds:

- 64-bit DLL → SotFS (`Game/` next to `DarkSoulsII.exe`)
- 32-bit DLL → vanilla

Proton: `WINEDLLOVERRIDES="dinput8.dll=n,b" %command%`

## Pointer sources

DS2S-META, SoulMemory, Radai/Atvaark CE tables. See `tools/POINTERS.md`.

## Testing checklist

- Cold boot → main menu (credit on, no laughs)
- Load character → no wipe/cheer storm
- Chip damage / Estus / death
- Last Giant fog → cheer; kill → applause; re-entry after death → cheer again
- Majula / Forest of Fallen Giants title → one wipe
- Ground pickup ooh; bonfire Estus refill silent
- Empty flask / locked door / no spell / ladder fall
- Alt-tab, load screens, NG+ (already-on flags must not fire on load)
- Proton `WINEDLLOVERRIDES`
