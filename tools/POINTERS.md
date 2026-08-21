# Pointer notes — DSR Sitcom

Offsets and AOBs are sourced from community research (Phokz cheat tables, DSR-Gadget, vanilla EMEVD). Credit to those authors.

Record the game build you tested against here after first successful run:

| Field | Value |
| --- | --- |
| Steam build / app ver | _(fill in)_ |
| `DarkSoulsRemastered.exe` size | _(fill in)_ |
| SHA256 | _(fill in)_ |
| Date tested | _(fill in)_ |

## BaseB (player vitals)

- **AOB:** `48 8B 05 ?? ?? ?? ?? 45 33 ED 48 8B F1 48 85 C0`
- `ChrStat = *(BaseB + 0x10)`
- HP / MaxHP / Deaths at `ChrStat+0x14`, `+0x1C`, `BaseB+0x98`

## BaseE (area / world)

- **AOB:** `48 8B 05 ?? ?? ?? ?? 48 8B 88 98 0B 00 00 8B 41 3C C3`
- Area / World bytes at `BaseE+0xA22` / `+0xA23`
- `255/255` treated as menu/load (not gameplay)
- **Not used for scene wipe** — those bytes are load-zone IDs and thrash at chunk boundaries

## MenuMan (area title card / Display Banner)

- **AOB (RemasterCETable, preferred):** `48 8B 05 ?? ?? ?? ?? 89 88 28 08 00 00 85 C9`
- **Fallback (DSR-Gadget long):** `48 8B 05 ?? ?? ?? ?? 89 88 28 08 00 00 85 C9 ?? ?? C7 80 34 08 00 00 FF FF FF FF C3`
- Pointer: `MenuMan = *(slot)` (one deref)
- **Message** byte at `MenuMan + 0x104` — EMEVD Display Banner IDs (not item pickup):
  - `1` Victory, `2`/`6` You Died, `8` Current Location, `13` Bonfire Lit, …
- Scene wipe = rising edge of Message → `8`
- **Title screen:** `int32` at `MenuMan + 0x80` == `1` → credit overlay

## Item get (ooh)

- **Trigger:** CurrentAnim rising edge to **7520** (ground pickup) or **7522** (chest pickup)
- ItemGet hook is kept only for debug drain logs — it fires for many non-pickup grants and is
  **not** used for ooh (was causing random triggers)

## Player anim (fail laughs)

- **WorldChrBase** → `ChrData1 = *(WorldChr)+0x68`
- **CurrentAnim** = `*(*(*(ChrData1+0x68)+0x48)+0x80)` (TAE id) — DSR-Gadget
- **StayAnimID** upper/lower = RemasterCETable  
  `*(*(*(ChrData1+0x48)+0x20)+0x2A0)` / `+0x498` (also TAE ids — **not** ESD state indices)
- Triggers (rising edge):
  - Empty flask: CurrentAnim **7588** / **7589** (full drink is 7585→7586→7587 — ignored)
  - Locked / invalid use: CurrentAnim **7510**
  - No spell: CurrentAnim **6299/6399**
  - Ladder fall: CurrentAnim **7050–7052** / **1560**

## Lock-on / Smough bonk

- **LockTGTBase** AOB: `48 8B 0D ?? ?? ?? ?? 89 99 ?? ?? ?? ?? 4C 89 6D 58`
- Lock-on byte at `*(LockTGTBase) + 0x1431` (RemasterCETable)
- Smough model id **c2360** (2360) when a lock-target ModelID reader is available
- Current bonk trigger: during O&S fight, hit dealing ≥ `bonk_min_damage_frac` of max HP  
  (Ornstein pokes tend to be smaller; Smough hammers larger)

## Special boss SFX

| Event | Signal |
| --- | --- |
| Sif boo | Defeat flag **5** rising edge → `Boo` (skips applause) |
| Pinwheel trombone | Player death while cheer **11305392** / defeat **6** fight active |
| Bed of Chaos death | Player death while cheer **11415392** / defeat **10** fight active → `BoCDeath.wav` |
| O&S bonk | Defeat **12** / cheer **11515392** fight + heavy hit |

## Fail-state laughs

Enabled by default. Driven by StayAnim (ESD) / CurrentAnim rising edges — see above.  
`laugh_on_locked_door` also matches general invalid-use shrug (may be noisy).

## Sound Effect volume

- One-shot hook on FMOD C++ `EventSystem::update`, then poll `SE` category `GetVolume`
- Never `LoadLibrary` FMOD; never probe guessed EventSystem pointers
- Final gain = `config.ini volume` × FMOD `SE` volume (0–1)

## Title credit

- When `MenuMan+0x80 == 1`, hook `IDXGISwapChain::Present` and blit GDI text via  
  `CopySubresourceRegion` (no `d3dcompiler`)

## Event flags (boss cheer / applause)

- **AOB (DSR-Gadget / EventPocket):** `48 8B 0D ?? ?? ?? ?? 99 33 C2 45 33 C0 2B C2 8D 50 F6`
- Pointer chain: `bitfield = **(slot)` (two derefs), then DSR-Gadget `getEventFlagOffset` bit packing

Baselines are re-seeded whenever you leave/re-enter gameplay so already-on defeat flags don’t fire on load.

## Failure modes

If an AOB misses after a patch, set `log=true` and check `sitcom/sitcom.log`.
