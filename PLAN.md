# DSR Sitcom — Implementation Plan

Sitcom canned-audience mod for **Dark Souls Remastered** (PC).

## Goal

Play overlay SFX on game events:

| Event | Sound |
| --- | --- |
| Player takes damage | Laughter |
| Player dies | Laughter (stronger / longer variant OK) |
| Boss health bar appears | Cheering |
| Boss dies | Applause |
| Area title card appears | Family Guy–style scene wipe (original soundalike for distribution) |

**Non-goals (v1):** online safety guarantees, ModEngine dependency, editing game soundbanks, visual overlays, configurable per-boss sound packs.

---

## Approach (chosen)

**`dinput8.dll` proxy + background poller + local audio player.**

Why this over alternatives:

- Covers all five events (soundbank swaps cannot reliably hit area title card / boss bar).
- Lowest install friction: drop files in game dir, launch normally.
- Proton-friendly with one launch option (`WINEDLLOVERRIDES="dinput8.dll=n,b"`).
- No ModEngine required for the gag itself.

Rejected for v1:

- **Soundbank mashup (Nexus 767-style):** incomplete event coverage, conflicts with other audio mods.
- **ModEngine extension / `external_dlls` only:** extra install step; still need the same DLL logic.
- **External watcher `.exe`:** easier to prototype, worse install UX (two processes / launcher script). May use briefly as a spike, then fold into the DLL.

---

## Player install (target UX)

```text
DARK SOULS REMASTERED/
  DarkSoulsRemastered.exe
  dinput8.dll                 # this mod (DirectInput8 proxy)
  sitcom/
    config.ini
    sounds/
      laugh_01.wav
      laugh_02.wav
      cheer_01.wav
      applause_01.wav
      scene_wipe.wav
```

1. Copy `dinput8.dll` + `sitcom/` into the game folder.
2. Launch the game as usual (Steam).
3. **Linux / Proton:** set launch options to  
   `WINEDLLOVERRIDES="dinput8.dll=n,b" %command%`
4. If another mod already uses `dinput8.dll`, document conflict / chain-load later (v1.1).

Offline / single-player recommended. Injected DLLs + Softs anticheat = undefined.

---

## Repo layout (to create)

```text
DSR_Sitcom/
  PLAN.md                 # this file
  README.md               # install + build (after scaffold)
  CMakeLists.txt
  src/
    dllmain.cpp           # DllMain, start/stop worker
    dinput8_proxy.cpp     # forward to system dinput8.dll
    dinput8_proxy.h
    game_state.cpp        # read HP / boss / area UI state
    game_state.h
    events.cpp            # edge detect + cooldowns → play requests
    events.h
    audio.cpp             # play WAVs (miniaudio or XAudio2)
    audio.h
    config.cpp            # sitcom/config.ini
    config.h
    log.cpp               # sitcom/sitcom.log (optional, behind config)
    log.h
  sitcom/                 # shipped defaults (copied next to built DLL)
    config.ini
    sounds/               # placeholders / licensed clips
  third_party/
    miniaudio.h           # or similar single-header
  tools/                  # optional CE notes, pointer docs
    POINTERS.md
```

Language: **C++17**, Windows x64 DLL. Build with CMake + MinGW or MSVC (document both; CI later optional).

---

## Runtime architecture

```text
Game loads dinput8.dll
        │
        ├─ Proxy: LoadLibrary(system dinput8) + forward exports
        │
        └─ Worker thread
              ├─ Load config + sound banks
              ├─ Resolve game base + pointer chains / AOB patterns
              └─ Loop (~20 Hz)
                    Read GameSnapshot
                    Diff vs previous → Event
                    Apply cooldown / once-per-fight / once-per-area rules
                    Audio::Play(category)
```

Keep game thread work near zero: only start the worker from `DllMain`/`DLL_PROCESS_ATTACH` (or first proxy call if attach proves fragile). Prefer delaying heavy init until the game window/process is ready (short sleep or wait for module init).

---

## Event detection (v1 strategy: memory polling)

Prefer **polling stable game state** over deep function hooks. Hooks are a later optimization.

### Snapshot fields (targets)

| Field | Use |
| --- | --- |
| `player_hp`, `player_max_hp` | Hit (delta down while `hp > 0`), death (`hp == 0` edge) |
| `player_loaded` / char base valid | Ignore menus / title screen |
| `boss_bar_active` | Cheer on false→true |
| `boss_hp` (optional) | Applause on active + hp→0; or use “bar went inactive after fight” heuristic |
| `area_id` and/or `area_name_ui_visible` | Scene wipe on new area title card |

Exact offsets/AOBs are **TBD** and version-sensitive. Capture them against a pinned DSR build (document Steam build / exe hash in `tools/POINTERS.md`).

### Research sources (do not reinvent blindly)

- DSR-Gadget / community CE tables (player HP, death)
- Existing DSR `dinput8` trackers / overlays (pattern-scan examples)
- Cheat Engine session while triggering: take hit, die, open boss, kill boss, cross area boundary for title card

### Edge rules

| Event | Trigger | Anti-spam |
| --- | --- | --- |
| Laugh (hit) | `hp` decreased, `hp > 0` | Cooldown 1.0–2.0s (config) |
| Laugh (death) | `hp` became 0 or death flag | Always allow; ignore hit laugh briefly after |
| Cheer | Boss bar inactive→active | Once until bar clears |
| Applause | Boss fight end (hp 0 or bar clear after combat) | Once per fight; don’t double with cheer |
| Scene wipe | Area title card show / `area_id` change with UI | Once per `area_id` until leave+return if desired |

False positives to watch: loading screens, riposte self-damage quirks, multi-bar bosses (O&S), quitting to menu mid-fight.

---

## Audio

- Format: WAV (PCM), short clips (0.5–4s).
- Multiple variants per category; pick random excluding last-played when possible.
- Master volume + per-category volume in `config.ini`.
- Mix **with** game audio (do not mute game).
- Library: **miniaudio** (single header, fewer Proton/XAudio edge cases) unless XAudio2 proves simpler on Windows-only testing.

Placeholder silent/beep files in repo until real assets land. For Nexus release: **no copyrighted Family Guy audio** — ship an original wipe SFX.

---

## Config (`sitcom/config.ini`)

```ini
[sitcom]
enabled=true
log=false
poll_hz=20
volume=0.7

[events]
laugh_on_hit=true
laugh_on_death=true
cheer_on_boss_bar=true
applause_on_boss_death=true
wipe_on_area_title=true

[cooldowns]
laugh_hit_seconds=1.5

[paths]
sounds_dir=sounds
```

Paths relative to `sitcom/` next to the DLL (resolve via DLL module path, not CWD).

---

## Implementation milestones

### M0 — Repo scaffold
- [ ] Git init (if not already)
- [ ] CMake project producing `dinput8.dll`
- [ ] Minimal DirectInput8 proxy (load real DLL, forward exports)
- [ ] Worker thread + log file proving load on game start
- [ ] Default `sitcom/` tree copied beside output

### M1 — Audio path
- [ ] Load WAVs from `sitcom/sounds/`
- [ ] `Play(category)` with volume + random variant
- [ ] Manual test: key or timer fire from worker (remove before release)

### M2 — Player hit / death
- [ ] Resolve player HP pointer / AOB for target build
- [ ] Hit + death edge detection + cooldowns
- [ ] Document offsets in `tools/POINTERS.md`

### M3 — Boss bar + boss death
- [ ] Detect boss bar active + boss HP or fight-end heuristic
- [ ] Cheer / applause once-per-fight rules
- [ ] Spot-check Asylum Demon, then a dual-bar fight if feasible

### M4 — Area title card
- [ ] Find reliable signal (UI flag vs area ID + timing)
- [ ] Scene wipe once per transition
- [ ] Verify Firelink → Undead Burg style transitions

### M5 — Polish + package
- [ ] Config flags for each event
- [ ] README: Windows install, Proton line, anticheat note, dinput8 conflicts
- [ ] Zip layout matching install UX
- [ ] Replace placeholder sounds with licensed/original assets

---

## Testing checklist

- Cold boot → main menu (no sounds)
- Load character → no false wipe
- Take chip damage → laugh; spam hits → cooldown respected
- Die → death laugh
- Enter Asylum Demon fog → cheer when bar appears; kill → applause
- Cross into new named area → one wipe with title card
- Alt-tab / pause → no crash; no sound storm
- Proton: DLL loads with `WINEDLLOVERRIDES`

---

## Risks

| Risk | Mitigation |
| --- | --- |
| Patch breaks offsets | AOB scans + version note in README / POINTERS.md |
| Boss / area signals hard to find | Spike with CE first; ship hit/death if M3/M4 slip |
| `dinput8` conflict with other mods | Document; optional rename + ModEngine `external_dlls` later |
| Proton audio / path issues | Resolve paths from DLL dir; prefer miniaudio |
| Softs / online bans | Offline recommendation only |

---

## Open questions (resolve during M0–M2)

1. Pin which DSR Steam build / exe version we support first?
2. MSVC vs MinGW for the primary build (affects Proton testing on this machine)?
3. Accept “boss music started” as cheer fallback if bar flag is elusive?
4. Scene wipe on **every** area card, or only the first discovery of an area?

---

## Immediate next actions

1. Scaffold CMake + `dinput8` proxy + worker log (M0).
2. Add miniaudio + config + placeholder sounds (M1).
3. CE session: capture player HP chain for hit/death (M2 research).
4. Only then chase boss bar + area title signals.
