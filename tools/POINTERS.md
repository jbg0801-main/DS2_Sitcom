# Pointer notes — DS2 Sitcom

Offsets and AOBs are sourced from DS2S-META (pseudostripy / Nordgaren), SoulMemory / SoulSplitter (Frank van der Stam), Lord Radai / Atvaark CE tables, and the soulsmodding character-id list. Credit to those authors.

Record the game build you tested against here after first successful run:

| Field | SotFS | Vanilla |
| --- | --- | --- |
| Steam app | 335300 | 236430 |
| Typical `DarkSoulsII.exe` size | 1.03 `0x1D76000` / 1.02 `0x20B6000` | 1.12 `0x01DB4000` / 1.11 `0x02055000` |
| SHA256 | _(fill in)_ | _(fill in)_ |
| Date tested | _(fill in)_ | _(fill in)_ |

## Flavor

- 64-bit DLL → Scholar offsets. 32-bit DLL → vanilla offsets.
- Module size is logged; unknown sizes still attempt the edition AOBs.

## BaseA (GameManagerImp)

- **SotFS AOB:** `48 8B 05 ?? ?? ?? ?? 48 8B 58 38 48 85 DB 74 ?? F6` (fallback `48 8B 35 ?? ?? ?? ?? 48 8B E9 48 85 F6`)
- **Vanilla AOB:** `8B F1 8B 0D ?? ?? ?? ?? 8B 01 8B 50 28 FF D2 84 C0 74 0C` (absolute pointer at +4)
- `PlayerCtrl = *(BaseA + 0xD0)` SotFS / `+ 0x74` vanilla
- HP / HPMax at PlayerCtrl `+0x168/+0x170` SotFS, `+0xFC/+0x104` vanilla
- Deaths at `PlayerParam` (PlayerCtrl `+0x490` / `+0x378`) then `+0x1A4` / `+0x1A0`
- GameState int32 at BaseA `+0x24AC` SotFS / `+0xDEC` vanilla  
  `0x1E` = in game, `0x0A` = main menu (DS2S-META `GAMESTATE`)

## LoadState

- **SotFS AOB:** `48 89 05 ?? ?? ?? ?? B0 01 48 83 C4 28` then int32 at `+0x11C == 1` while loading (SoulMemory)
- **Vanilla AOB:** `89 35 ?? ?? ?? ?? E8 ?? ?? ?? ?? 66 0F EF C0` then `+0x1D4 == 1`

## Event flags (boss applause)

SoulMemory walk (not DSR 8-digit packing).

- SotFS: GameManagerImp chain `0, 0x70, 0x20` then `*(base + r8*8 + 0x20)` buckets, next `+0x10`
- Vanilla: chain `0, 0, 0x44, 0x10`, buckets `+0x10`, next `+0xC`
- Defeat IDs are 6-digit (Last Giant `100971`, Pursuer `100968`, … Ivory King `101070`). See SoulSplitter wiki “Dark Souls 2 misc flags”.
- Baselines re-seeded whenever you leave/re-enter gameplay.

## Boss cheer (LoadedEnemiesTable)

DS2 has no DSR-style EMEVD “display boss bar” flags. v1 cheers when a known boss **chr id** appears in `LoadedEnemiesTable` (`*(BaseA+0x18)` SotFS; same hop attempted on vanilla) while that boss’s defeat flag is still off. Chr id field is probed at several struct offsets (`+0x28`, `+0x14`, …) for 32-bit layout differences. Vanilla table is not in META — if cheer stays silent, CE the HUD boss gauge / fog bit and record it here.

## Area wipe (PlaceName title card)

Map ids and FMOD zone SE are **not** the title card.

**Current approach (SotFS 1.03):**
- Hook `FeSubStateTitleInformation` vtable `[1]` activate (`RVA 0xFF570`) — first PlaceName
- Capture instance; poll `+0x10` for idle `{0,-1,4}` → show-start `{1,2,5}` (Majula etc.)

Log: `area_title: state A→B` / `PlaceName show` then scene_wipe.

## Event flags / boss counters

- SoulMemory chains: EventFlagManager `BaseA → +0x70 → +0x20`, boss kills `→ +0x70 → +0x28 → +0x20 → +0x8`.
- Prefer SoulMemory GameManagerImp AOB `48 8B 35 … 48 8B E9 48 85 F6`.
- EventManager is often **null on the title screen** — resolve again once `player_valid`.

## Boss applause

- Primary: 6-digit defeat event flags (SoulMemory walk). **Important:** resolve EventFlagManager with a final pointer deref (SoulMemory `AddPointer` convention).
- Backup: SoulMemory boss kill-count array  
  - SotFS: `BaseA → +0x70 → +0x28 → +0x20 → +0x8`  
  - Vanilla: `BaseA → +0 → +0x44 → +0x14 → +0x10 → +0x4`  
  - `BossType` byte offsets in SoulMemory `BossType.cs` (Last Giant `0x7c`, …)

## Player anim (fail laughs)

Probe chains under `PlayerCtrl` (`+0xB8 → +0x8 → +0x20 → +0xC` and nearby). With `laugh_on_empty_flask=true`, every anim change is logged as `probe: anim A→B` until TAE IDs are pinned.

## ItemGive (ooh)

- **SotFS AOB:** `48 89 5C 24 18 56 57 41 56 48 83 EC 30 45 8B F1 41`
- **Vanilla AOB:** `55 8B EC 83 EC 10 53 8B 5D 0C 56 8B 75 08 57 53 56 8B F9`
- Filtered restock ids: Estus `0x0395E478`, empty Estus `0x0395E860`, Sublime Bone Dust `0x039B8DB0`
- Pickup anim 7520/7522 kept as a tentative second trigger — confirm TAE in CE (`log=true`)

## Player anim (fail laughs)

Tentative SotFS chain `PlayerCtrl+0xB8 → +0x8 → +0x20 → +0xC`. Values outside 0–19999 are ignored. Probe logs goods-band 7400–7600. Empty flask / locked use / fail-cast / ladder-fall still use DS1-like TAE numbers until CE confirms DS2 ids.

## Sound Effect volume

- SotFS: one-shot hook on `fmod_event64.dll` `EventSystem::update` (`?update@EventSystem@FMOD@@QEAA…`)
- Vanilla: `fmod_event.dll` / `fmodex.dll` (`?update@EventSystem@FMOD@@QAE…`)
- Probe categories `SE`, `SFX`, `master/SE`
- Never `LoadLibrary` FMOD; never probe guessed EventSystem pointers
- Final gain = `config.ini volume` × FMOD category volume (0–1)

## Title credit

- SotFS: D3D11 `IDXGISwapChain::Present` blit when GameState is main menu
- Vanilla: D3D9 `IDirect3DDevice9::EndScene` GDI text via `GetDC`

## Failure modes

If an AOB misses after a patch, set `log=true` and check `sitcom/sitcom.log`.
