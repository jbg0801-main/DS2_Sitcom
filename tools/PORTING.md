# Porting Sitcom to another FromSoftware game

Instructions for an AI (or human) converting **DSR_Sitcom** or **DS2_Sitcom** into a new overlay for **Dark Souls III**, **Elden Ring**, **Sekiro**, etc.

Do **not** invent pointers. Research community memory layouts first, then rewrite only the game-facing layer. Keep the audio/proxy shell.

---

## 0. Pick the source repo

| Target game | Prefer cloning from | Why |
| --- | --- | --- |
| DS3, Elden Ring, Sekiro, Armored Core VI (64-bit DX12/11) | **DSR_Sitcom** | Single 64-bit ABI; event-flag style closer to DSR/DS3 gadget walks than DS2’s linked-list flags |
| Another dual-ABI / oddball engine | **DS2_Sitcom** | Already has flavor tables, x86 stubs, dual packaging |

Default for DS3 / ER: copy `DSR_Sitcom` → `DS3_Sitcom` / `EldenRing_Sitcom`.

Ask the user once which **v1 events** to keep (hit/death laugh, boss cheer, boss applause, area wipe, item ooh, fail laughs). Drop DSR-only gags (Sif/Pinwheel/BoC/Smough) unless they name replacements.

---

## 1. Freeze the architecture (do not redesign)

Keep these files almost unchanged:

- `src/dllmain.cpp` — worker thread, ~20 Hz poll, smoke-test laugh
- `src/dinput8_proxy.cpp` / `exports.def` — system `dinput8` forwarder + **chain-load** (`[chainload] dlls=` / `sitcom/chainload.txt`)
- `src/audio.cpp` / `audio.h` — WAV categories + winmm gain
- `src/log.cpp`, `src/paths.cpp`, `src/config.cpp` (rename game-specific keys only)
- `sitcom/sounds/` layout and prefix naming (`laugh_`, `cheer_`, `applause_`, `ooh_`, `scene_wipe_`)
- `sitcom/chainload.txt` — preserve chain-load UX when porting

Rewrite these for the new game:

- `src/game_state.cpp` / `.h` — module wait, AOBs, snapshot
- `src/event_flags.cpp` / `.h` — flag reader matching that game’s packing
- `src/events.cpp` / `.h` — rising-edge detectors wired to new snapshot fields
- `src/item_hooks.cpp` / `.h` — ItemGet / ItemGive AOB + restock filters
- `src/fmod_volume.cpp` — FMOD DLL name + mangled `EventSystem::update` (or skip if unused)
- `src/credit.cpp` — Present/EndScene path matching the renderer
- CMake project name, zip names, README, `tools/POINTERS.md`

Optional: add `src/<game>_flavor.h/.cpp` only if multiple patches/ABIs need different offsets (like DS2 SotFS vs vanilla).

---

## 2. Research checklist (do this before coding)

Fill a scratch table in `tools/POINTERS.md` before touching AOBs:

1. **Process / install**
   - Exact exe name (`DarkSoulsIII.exe`, `eldenring.exe`, …)
   - Steam app id(s)
   - Drop folder next to the exe
   - Bitness (almost always **64-bit** for DS3/ER)
   - Renderer (DS3: DX11; ER: DX12 — credit hook differs)
   - Existing `dinput8` mods that will conflict
   - Softban / EAC notes (ER uses Easy Anti-Cheat online — **offline only**)

2. **Community sources to mine** (prefer reading source, not guessing)
   - SoulMemory / SoulSplitter (Frank van der Stam)
   - Practice tools: DS3S-META / ER practice tools / CE tables
   - Speedrun / autosplit wiki flag lists
   - soulsmodding wiki for chr / map / item IDs

3. **Minimum memory map you must find**

| Need | Typical names | Used for |
| --- | --- | --- |
| World / game manager singleton | BaseB / WorldChrMan / GameDataMan | Root of most chains |
| Player HP + HPMax | PlayerIns / ChrData | Hit / death laugh |
| Death counter or HP→0 | same / GameData | Death laugh |
| In-game vs title / loading | MenuMan / CSMenuMan / LoadState | Gate events; title credit |
| Boss defeated | Event flags or boss clear bits | Applause |
| Boss bar / fight started | EMEVD cheer flags, HUD gauge, fog bit, or loaded boss chr | Cheer |
| Area title / map change | Banner enum, map id, area number | Scene wipe |
| Item granted | ItemGet / AwardItem / similar | Ooh |
| Player current anim / TAE | ChrIns anim | Fail laughs + pickup backup |
| FMOD module | `fmod_event64.dll` etc. | Volume match |
| Swap chain / Present | D3D11 or DXGI/DX12 | Title credit |

4. **Flag packing**
   - DSR / DS3-style: often 8-digit IDs in bitfield arrays (Gadget / SoulMemory)
   - DS2-style: 6-digit linked-list walk (do **not** reuse on DS3/ER)
   - Elden Ring: SoulMemory event-flag API — port that walk, do not invent

5. **Item restock filter**
   - New item IDs for Estus / flask / rune / arrow auto-refills
   - Never reuse DSR `200–215` or DS2 `0x0395E478` blindly

---

## 3. Implementation order (same milestones every port)

Work SotFS-first style: get one flavor booting, then extras.

1. **Scaffold**
   - Rename CMake project / zip / README
   - Wait loop: `GetModuleHandleW(L"<ExactExe>")` until non-null
   - Heartbeat log: `in_game`, `title`, `loading`, `hp`, `anim`
   - Strip previous game’s AOBs so wrong-exe loads fail cleanly
   - Mark todos in_progress → completed as you go; do not recreate them

2. **HP / death / menu gating**
   - Resolve player HP + max HP + deaths
   - Define `in_gameplay` = player valid && !loading && not title
   - Wire existing laugh cooldowns

3. **Boss applause**
   - Port that game’s `ReadEventFlag`
   - Seed baselines on enter-gameplay (prevents NG+/load storms)
   - Defeat ID list from SoulSplitter wiki or equivalent

4. **Boss cheer**
   - Prefer “boss bar appeared” / fog / fight-started signal
   - Last resort: boss chr appears in world while defeat flag off
   - Never cheer on map enter alone

5. **Area wipe**
   - Prefer area-name banner state (DSR used MenuMan banner == 8)
   - Else debounce map/area id rising edge while `!loading`
   - Document CE unknowns in `POINTERS.md`

6. **Item ooh + fail laughs**
   - Hook ItemGive/ItemGet with correct ABI trampoline (14-byte x64; 5/6-byte x86 if needed)
   - Filter restocks
   - CE for pickup + fail TAE IDs; log anim band behind `log=true` until pinned
   - Do not copy another game’s TAE numbers without confirmation

7. **FMOD + credit**
   - Capture already-loaded FMOD — never `LoadLibrary`
   - Probe category names (`SE` / `SFX` / `master/SE`)
   - Credit: D3D11 Present (DS3), DXGI Present/DX12 path (ER), D3D9 EndScene only if DX9

8. **Package**
   - One zip per ABI if needed
   - Proton note: `WINEDLLOVERRIDES="dinput8.dll=n,b" %command%`
   - Offline recommended

---

## 4. Snapshot contract (keep this shape)

`GameSnapshot` should expose stable fields for `EventDetector`, even if internals change:

```text
player_valid, in_gameplay, is_loading, on_title_screen
player_hp, player_max_hp, deaths
boss_fight_active / cheer signal / cheer ids
defeat_flags_on[]          // rising edge → applause
area_valid, area_id        // rising edge → wipe (or banner enum)
current_anim, anim_valid   // fail laughs / pickup backup
```

`EventDetector` stays rising-edge + cooldowns + gameplay seeding. Prefer changing snapshot producers over rewriting event logic.

---

## 5. Hard rules (from DSR → DS2)

- One DLL cannot serve two ABIs (32 vs 64). Dual-build if needed.
- Wrong exe name → every AOB fails. Fix the wait loop first.
- Different engines → different flag walks. Copy SoulMemory, do not reuse the previous game’s bit math.
- Seed event-flag baselines on gameplay enter; clear on leave.
- Never ship cheer-on-map-enter.
- Never `LoadLibrary` FMOD; only hook if the game already loaded it.
- If credit Present/EndScene fails, skip credit rather than crash.
- Do not edit the user’s plan file if they attached one.
- Do not commit unless asked.

---

## 6. Quick targets (starting hints)

### Dark Souls III
- Exe: `DarkSoulsIII.exe` (64-bit, DX11)
- Install: `Game/` next to exe
- Memory: WorldChrMan / GameDataMan style (SoulMemory DS3) — closer to DSR than DS2
- Flags: 8-digit EMEVD-style; SoulSplitter flag lists
- Credit: D3D11 Present path from DSR/DS2 SotFS
- Online: softban risk — offline recommended

### Elden Ring
- Exe: `eldenring.exe` (64-bit, DX12)
- EAC online — ship and test **offline / Seamless Coop-aware** only if user accepts risk; default offline
- Memory: SoulMemory EldenRing module (WorldChrMan, GameDataMan, EventFlagMan)
- Flags: ER event-flag packing from SoulMemory — port exactly
- Credit: DX12/DXGI Present is harder than D3D11; if stuck, skip credit on ER rather than crash
- Item / flask IDs differ completely from DS1/DS2

### Sekiro
- Exe: `sekiro.exe`
- SoulMemory / CE community tables; posture/death semantics differ (decide hit vs death laughs with user)

---

## 7. Playtest checklist (every flavor)

- Cold boot → main menu (credit on if hooked, no laughs)
- Load character → no wipe/cheer/applause storm
- Chip damage / heal / death
- First fog → cheer; kill → applause; die and re-enter → cheer again, no extra applause
- Area title / map change → one wipe
- Ground pickup ooh; bonfire/flask refill silent
- Empty flask / locked door / no spell / ladder fall (if enabled)
- Alt-tab, load screens, NG+
- Proton `WINEDLLOVERRIDES` if applicable

---

## 8. Done criteria

Port is done when:

1. Correct exe wait + flavor log
2. Hit/death laughs gated to gameplay
3. Applause on defeat flags with seeding
4. Cheer without map-enter false positives (or documented CE TODO)
5. Wipe on banner or map-id edge
6. Item ooh with restock filter (or documented missing AOB)
7. FMOD volume and/or credit working or explicitly skipped
8. README + `tools/POINTERS.md` updated with AOBs, flag IDs, tested exe size/hash blanks
9. Release zip builds for each ABI

---

## 9. One-shot prompt you can paste

```text
Port this Sitcom overlay to <GAME>. Clone from <DSR_Sitcom|DS2_Sitcom>.
Keep dinput8 proxy, worker, audio, config shell. Rewrite game_state,
event_flags, events, item_hooks, fmod_volume, credit, CMake, README,
tools/POINTERS.md. Research SoulMemory + practice-tool AOBs first — do not
invent pointers. v1 events: hit/death laugh, boss cheer, boss applause,
area wipe, item ooh, fail laughs. Offline recommended. Follow
tools/PORTING.md milestone order. Mark existing todos in_progress as you
work; do not recreate them; do not edit the attached plan file.
```
