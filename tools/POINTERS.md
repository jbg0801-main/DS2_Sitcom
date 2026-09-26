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
- Vanilla 1.12: `*(BaseA+0x44)+0x10` (chain `0, 0x44, 0x10`). The old extra leading `0` followed the vtable and never found a flag group. Buckets still `+0x10`, next `+0xC`
- Defeat IDs are 6-digit (Last Giant `100971`, Pursuer `100968`, … Ivory King `101070`). See SoulSplitter wiki “Dark Souls 2 misc flags”.
- Baselines re-seeded whenever you leave/re-enter gameplay.

## Boss cheer (ActiveBossBattleId)

LoadedEnemiesTable chr presence is **obsolete** (bosses preload with the area).

**Primary:** ESD `IsBossBattle` chain.

```text
SotFS 1.03:  BaseA → *(+0x70) EventManager → *(+0x88) BossBattleState → int32(+0x14)
Vanilla 1.12: BaseA → *(+0x44) EventManager → *(+0x44) BossBattleState → int32(+0x10)
```

Vanilla site is `0x8678F0`.

Cheer on rising edge `0 → nonzero`. Latch while id stays set; clear when id returns to 0
(bonfire / death outside fog can re-arm).

**TRACE (optional):** `trace_boss_bar=true` hooks `FeSceneBossHpGuage` vt[4]
(`vtable 0x10B0C68`, slot `0x10B0C88`, tick `0x60F00`) and logs `+0xD0/+0xB8…` field changes.
Do **not** hook `FeSceneEnemyHpGuage` (lock-on trash).

Bob CT Fog Walls bits are fog-cleared world flags — not fight-start.

## Area wipe (PlaceName title card)

Map ids and FMOD zone SE are **not** the title card.

**Current approach:** hook `FeSceneMapName` vt[4] and wipe when the displayed PlaceName
text id changes (`>= 100000`).

- SotFS 1.03: vt `0x10FA7E0`, vt[4] `RVA 0x684F0` / slot `0x10FA800`, displayed id `+0x1C` (pending `+0x18`)
- Vanilla 1.12: vt `0xEE9130`, vt[4] `RVA 0x128140` / slot `0xEE9140`, displayed id `+0x10` (pending `+0xC`).
  The tick is `thiscall` and ends with `ret 4`.

Observed (SotFS): Things Betwixt `100200`, Majula `100400`, Heide `103100`, No-man's Wharf `101800`.
Clear-to-`-1` (vt[3]) is ignored. Ids outside `100000..9999999` are logged as `unmapped` and do not wipe.
Connector sub-maps that change the displayed id with no title card are ignored:
`103000` (`m10_30` Heide ↔ Wharf spiral) and `102900` (`m10_29` Majula ↔ Shaded Woods).

`FeSubStateTitleInformation` activate is first-load-only — **not** Majula walks.

**Diagnosis:** set `trace_area_title=true` to log extra `area_trace:` lines. Turn it off afterwards.

## Event flags / boss counters

- SoulMemory chains: EventFlagManager `BaseA → +0x70 → +0x20`, boss kills `→ +0x70 → +0x28 → +0x20 → +0x8`.
- Prefer SoulMemory GameManagerImp AOB `48 8B 35 … 48 8B E9 48 85 F6`.
- EventManager is often **null on the title screen** — resolve again once `player_valid`.

## Boss applause

- Primary: 6-digit defeat event flags (SoulMemory walk). **Important:** resolve EventFlagManager with a final pointer deref (SoulMemory `AddPointer` convention). The seen-flag set resets only on the title screen, so a kill that arrives on the frame gameplay returns still applauds. While `ActiveBossBattleId` is set, a new defeat flag is logged but the clap waits until that id falls to 0 and loot landed within 4s (Dragonrider: flag `100959` under the death sting, clap ~3s later with the soul). Flag/kill-count still applaud immediately when no fight is active.
- Backup: SoulMemory boss kill-count array  
  - SotFS: `BaseA → +0x70 → +0x28 → +0x20 → +0x8`  
  - Vanilla: `BaseA → +0x44 → +0x14 → +0x10 → +0x4` (no extra vtable hop)  
  - `BossType` byte offsets in SoulMemory `BossType.cs` (Last Giant `0x7c`, …)

## Player anim (fail laughs)

Bob SotFS CT chain (Current Animation):
`PlayerCtrl +0xF8 → +0x38 → +0x78 → +0x20 → i32@+0x10`

Vanilla 1.12 (DebugView, still true for HP `+0xFC` and PlayerData `+0x378`):
`PlayerCtrl +0xB4` (ChrMotionCtrl) → `+0x28` (MorphemeMotionCtrl). The TAE id is not at
the SotFS leaf, so the poll also checks i32s at `+0x10..+0x24` on that object and one
pointer hop below it. A hit logs `anim: vanilla A→B via …`. Empty shake is still `180202`.

With `laugh_on_empty_flask=true`, every anim change logs `probe: anim A→B` until empty-flask TAE is pinned.

## ItemGive (ooh)

- **SotFS AOB:** `48 89 5C 24 18 56 57 41 56 48 83 EC 30 45 8B F1 41`
- **Vanilla AOB:** `55 8B EC 83 EC 10 53 8B 5D 0C 56 8B 75 08 57 53 56 8B F9`
- Filtered restock ids: Estus `0x0395E478`, empty Estus `0x0395E860`, Sublime Bone Dust `0x039B8DB0`
- Pickup anim 7520/7522 kept as a tentative second trigger — confirm TAE in CE (`log=true`)

## Player anim (fail laughs)

Bob SotFS CT: `PlayerCtrl+0xF8 → +0x38 → +0x78 → +0x20 → +0x10`. Empty flask / locked use /
fail-cast / ladder-fall IDs still tentative — use `probe: anim` logs to pin them.

## Empty Estus

Primary signal is Bob Current Animation (chain `PlayerCtrl+0xF8→+0x38→+0x78→+0x20→+0x10`):
- empty shake: `180200 → 900 → **180202**`
- filled chug: `180200 → 900 → **180201** → 920`

The Estus charge helper (~`RVA 0x1AE080`) only runs on filled drinks (`0x1AE0BF` decrement).
Empty use never enters that function (fail branch at `0x1AE0D3` stays cold) — do not patch the
decrement site (`rax` clobber → infinite Estus).

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
