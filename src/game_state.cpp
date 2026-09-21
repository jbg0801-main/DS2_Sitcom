#include "game_state.h"

#include "ds2_flavor.h"
#include "event_flags.h"
#include "log.h"
#include "mem.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sitcom {
namespace {

EventFlags g_event_flags;

struct BossDef {
  std::int32_t defeat_flag;
  int boss_type_off;  // SoulMemory BossType enum value (byte offset into kill array)
  int chr_ids[4];
};

// Defeat flags: SoulSplitter wiki. BossType: SoulMemory BossType.cs. Chr: soulsmodding.
constexpr BossDef kBosses[] = {
    {100971, 0x7c, {3096, 0, 0, 0}},          // Last Giant
    {100968, 0x70, {3180, 0, 0, 0}},          // Pursuer
    {100953, 0x34, {6191, 0, 0, 0}},          // Executioner's Chariot
    {100958, 0x48, {5040, 0, 0, 0}},          // Looking Glass Knight
    {100954, 0x38, {1540, 0, 0, 0}},          // Skeleton Lords
    {100961, 0x54, {3033, 0, 0, 0}},          // Flexile Sentry
    {100963, 0x5c, {6260, 0, 0, 0}},          // Lost Sinner
    {101001, 0xa8, {3240, 0, 0, 0}},          // Belfry Gargoyles
    {100962, 0x58, {3250, 0, 0, 0}},          // Ruin Sentinels
    {100965, 0x64, {2261, 0, 0, 0}},          // Royal Rat Vanguard
    {100967, 0x6c, {6280, 0, 0, 0}},          // Royal Rat Authority
    {100957, 0x44, {5030, 0, 0, 0}},          // Scorpioness Najka
    {100951, 0x2c, {6030, 0, 0, 0}},          // Duke's Dear Freja
    {100956, 0x40, {5010, 0, 0, 0}},          // Mytha
    {100966, 0x68, {3260, 0, 0, 0}},          // The Rotten
    {100960, 0x50, {6250, 0, 0, 0}},          // Old Dragonslayer
    {100955, 0x3c, {5000, 5001, 0, 0}},       // Covetous Demon
    {100964, 0x60, {3050, 0, 0, 0}},          // Smelter Demon (red)
    {100952, 0x30, {6070, 0, 0, 0}},          // Old Iron King
    {100970, 0x78, {2120, 0, 0, 0}},          // Guardian Dragon
    {100950, 0x28, {6020, 0, 0, 0}},          // Demon of Song
    {100975, 0x8c, {3330, 0, 0, 0}},          // Velstadt
    {100978, 0x98, {5146, 6840, 0, 0}},       // Vendrick
    {100979, 0x9c, {5061, 0, 0, 0}},          // Darklurker
    {100959, 0x4c, {6110, 6115, 0, 0}},       // Dragonrider
    {100980, 0xa0, {6110, 6115, 0, 0}},       // Twin Dragonriders
    {101000, 0xa4, {1370, 0, 0, 0}},          // Prowling Magus
    {100972, 0x80, {3097, 0, 0, 0}},          // Giant Lord
    {100977, 0x94, {6000, 0, 0, 0}},          // Ancient Dragon
    {100974, 0x88, {3320, 3340, 0, 0}},       // Throne Watcher & Defender
    {100973, 0x84, {6270, 7036, 0, 0}},       // Nashandra
    {101080, 0x118, {6920, 0, 0, 0}},         // Aldia
    {101050, 0xc8, {6820, 0, 0, 0}},          // Elana
    {101051, 0xd4, {6810, 0, 0, 0}},          // Sinh
    {101053, 0xf4, {0, 0, 0, 0}},             // Graverobber trio
    {101063, 0xfc, {3052, 0, 0, 0}},          // Blue Smelter
    {101061, 0xcc, {6750, 0, 0, 0}},          // Fume Knight
    {101062, 0xf8, {6800, 0, 0, 0}},          // Sir Alonne
    {101070, 0x104, {6900, 0, 0, 0}},         // Burnt Ivory King
    {101071, 0xd0, {6791, 0, 0, 0}},          // Aava
    {101072, 0x108, {6790, 0, 0, 0}},         // Lud & Zallen
};

// Map ids appear in two encodings (both end in …00_00):
//   packed bytes  m10_04_00_00 → 0x0A040000 (LastSetBonfireAreaID style)
//   decimal digits m10_04_00_00 → 10040000  (WorldMapList / file style)
bool LooksLikePackedMapId(std::int32_t id) {
  const auto u = static_cast<std::uint32_t>(id);
  if ((u & 0xFFFFu) != 0) {
    return false;  // …_00_00
  }
  const auto hi = u >> 24;
  return hi == 0x0Au || hi == 0x14u || hi == 0x28u || hi == 0x32u;
}

bool LooksLikeDecimalMapId(std::int32_t id) {
  // e.g. 10040000 — reject loose matches like 20780552 (false +0x30 hit).
  if (id < 10000000 || id > 50999999 || (id % 10000) != 0) {
    return false;
  }
  const int aa = id / 1000000;
  return aa == 10 || aa == 20 || aa == 40 || aa == 50;
}

bool LooksLikeMapId(std::int32_t id) {
  return LooksLikePackedMapId(id) || LooksLikeDecimalMapId(id);
}

void CollectMapIdsFromObject(std::uintptr_t obj, int max_off, std::int32_t ignore_id,
                             std::vector<std::pair<std::int32_t, int>>* out) {
  if (!obj || !out) {
    return;
  }
  for (int off = 0; off <= max_off; off += 4) {
    const auto v = ReadT<std::int32_t>(obj + static_cast<std::uintptr_t>(off));
    if (!LooksLikeMapId(v) || v == ignore_id) {
      continue;
    }
    bool dup = false;
    for (const auto& e : *out) {
      if (e.first == v) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      out->push_back({v, off});
    }
  }
}

// Prefer a packed/decimal map id that is NOT LastSetBonfireAreaID (seamless travel).
bool TryReadCurrentMapId(std::uintptr_t base_a, const Ds2Offsets& off, std::int32_t* out_id,
                         int* out_tag) {
  std::int32_t bonfire = 0;
  const auto event_man =
      ReadPtr(base_a + static_cast<std::uintptr_t>(off.event_manager_from_base_a));
  if (event_man && off.last_bonfire_area_from_event_manager >= 0) {
    bonfire = ReadT<std::int32_t>(
        event_man + static_cast<std::uintptr_t>(off.last_bonfire_area_from_event_manager));
  }

  std::vector<std::pair<std::int32_t, int>> hits;

  if (off.map_manager_from_base_a >= 0) {
    const auto map_man =
        ReadPtr(base_a + static_cast<std::uintptr_t>(off.map_manager_from_base_a));
    if (map_man) {
      CollectMapIdsFromObject(map_man, 0x400, bonfire, &hits);
      // Child pointers at head of MapManager + MapItemPackManager (META +0x1c8).
      for (int i = 0; i < 6; ++i) {
        const auto child =
            ReadPtr(map_man + static_cast<std::uintptr_t>(i * static_cast<int>(sizeof(std::uintptr_t))));
        CollectMapIdsFromObject(child, 0x200, bonfire, &hits);
      }
      const auto pack = ReadPtr(map_man + 0x1c8);
      CollectMapIdsFromObject(pack, 0x200, bonfire, &hits);
    }
  }

  const auto gen = ReadPtr(base_a + 0x40);
  CollectMapIdsFromObject(gen, 0x200, bonfire, &hits);

  // CharacterManager / loaded-enemies root sometimes carries map context.
  if (off.loaded_enemies_from_base_a >= 0) {
    const auto chr =
        ReadPtr(base_a + static_cast<std::uintptr_t>(off.loaded_enemies_from_base_a));
    CollectMapIdsFromObject(chr, 0x100, bonfire, &hits);
  }

  static std::int32_t logged_sig = 0;
  if (!hits.empty()) {
    std::int32_t sig = static_cast<std::int32_t>(hits.size());
    for (const auto& h : hits) {
      sig ^= h.first;
    }
    if (sig != logged_sig) {
      logged_sig = sig;
      char buf[300];
      int n = snprintf(buf, sizeof(buf), "game_state: map candidates (ignore bonfire 0x%X):",
                       static_cast<unsigned>(bonfire));
      for (const auto& h : hits) {
        n += snprintf(buf + n, sizeof(buf) - static_cast<std::size_t>(n), " 0x%X",
                      static_cast<unsigned>(h.first));
        if (n >= static_cast<int>(sizeof(buf)) - 16) {
          break;
        }
      }
      LogWrite(buf);
    }
    *out_id = hits[0].first;
    *out_tag = hits[0].second;
    return true;
  }

  // Fall back to bonfire area only if nothing else exists (spawn before travel).
  if (LooksLikeMapId(bonfire)) {
    *out_id = bonfire;
    *out_tag = -1;  // marks bonfire fallback
    return true;
  }
  return false;
}

void MaybeLogMapProbe(std::uintptr_t /*map_man*/, std::int32_t map_id, int map_off) {
  static bool logged = false;
  if (logged) {
    return;
  }
  logged = true;
  char buf[220];
  snprintf(buf, sizeof(buf),
           "game_state: using map_id=%d (0x%X) tag=%d [%s]", map_id,
           static_cast<unsigned>(map_id), map_off,
           map_off < 0 ? "bonfire-fallback" : (LooksLikePackedMapId(map_id) ? "packed" : "decimal"));
  LogWrite(buf);
}

void DumpMapManagerWords(std::uintptr_t map_man) {
  if (!map_man || !IsReadable(map_man, 0x80)) {
    return;
  }
  char dump[320];
  int n = snprintf(dump, sizeof(dump), "game_state: MapManager words");
  for (int i = 0; i < 16 && n > 0 && n < static_cast<int>(sizeof(dump)) - 14; ++i) {
    const auto v = ReadT<std::uint32_t>(map_man + static_cast<std::uintptr_t>(i * 4));
    n += snprintf(dump + n, sizeof(dump) - static_cast<std::size_t>(n), " [%d]=0x%X", i, v);
  }
  LogWrite(dump);
}

void MaybeLogAreaMiss(std::uintptr_t base_a, const Ds2Offsets& off) {
  static bool logged = false;
  if (logged) {
    return;
  }
  logged = true;
  const auto map_man =
      off.map_manager_from_base_a >= 0
          ? ReadPtr(base_a + static_cast<std::uintptr_t>(off.map_manager_from_base_a))
          : 0;
  const auto event_man =
      ReadPtr(base_a + static_cast<std::uintptr_t>(off.event_manager_from_base_a));
  std::int32_t bonfire_area = 0;
  if (event_man && off.last_bonfire_area_from_event_manager >= 0) {
    bonfire_area = ReadT<std::int32_t>(
        event_man + static_cast<std::uintptr_t>(off.last_bonfire_area_from_event_manager));
  }
  char buf[280];
  snprintf(buf, sizeof(buf),
           "game_state: area probe miss MapManager=0x%llX EventManager=0x%llX "
           "LastSetBonfireAreaID=%d (0x%X)",
           static_cast<unsigned long long>(map_man), static_cast<unsigned long long>(event_man),
           bonfire_area, static_cast<unsigned>(bonfire_area));
  LogWrite(buf);
  DumpMapManagerWords(map_man);
  // Scan EventManager for any other packed map ids (possible "current area" leaf).
  if (event_man && IsReadable(event_man + 0x100, 0x100)) {
    char scan[240];
    int n = snprintf(scan, sizeof(scan), "game_state: EventManager map-like");
    for (int off = 0x100; off <= 0x1C0; off += 4) {
      const auto v = ReadT<std::int32_t>(event_man + static_cast<std::uintptr_t>(off));
      if (!LooksLikeMapId(v)) {
        continue;
      }
      n += snprintf(scan + n, sizeof(scan) - static_cast<std::size_t>(n), " +0x%X=0x%X", off,
                    static_cast<unsigned>(v));
      if (n >= static_cast<int>(sizeof(scan)) - 24) {
        break;
      }
    }
    LogWrite(scan);
  }
}

std::uintptr_t ScanBaseASlot() {
  const auto base = GameBase();
  const auto size = GameSize();
  if (!base) {
    return 0;
  }
  if (IsScholar()) {
    // Prefer SoulMemory AOB (EventManager/boss counters documented against it).
    const std::uint8_t pat_sm[] = {0x48, 0x8B, 0x35, 0, 0, 0, 0, 0x48, 0x8B, 0xE9, 0x48, 0x85,
                                   0xF6};
    const char mask_sm[] = "xxx????xxxxxx";
    auto hit = PatternScan(base, size, pat_sm, mask_sm);
    if (!hit) {
      const std::uint8_t pat_a[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x48, 0x8B, 0x58, 0x38,
                                    0x48, 0x85, 0xDB, 0x74, 0,    0xF6};
      const char mask_a[] = "xxx????xxxxxxxx?x";
      hit = PatternScan(base, size, pat_a, mask_a);
    }
    return hit ? ResolveRipMov(hit) : 0;
  }
  // SoulMemory uses last address byte = 01; keep wild so other patches still match.
  const std::uint8_t pat[] = {0x8B, 0xF1, 0x8B, 0x0D, 0,    0,    0,    0,    0x8B, 0x01, 0x8B,
                             0x50, 0x28, 0xFF, 0xD2, 0x84, 0xC0, 0x74, 0x0C};
  const char mask[] = "xxxx????xxxxxxxxxxx";
  const auto hit = PatternScan(base, size, pat, mask);
  if (!hit || !IsReadable(hit + 4, 4)) {
    return 0;
  }
  return static_cast<std::uintptr_t>(*reinterpret_cast<std::uint32_t*>(hit + 4));
}

std::uintptr_t ScanLoadStateSlot() {
  const auto base = GameBase();
  const auto size = GameSize();
  if (!base) {
    return 0;
  }
  if (IsScholar()) {
    const std::uint8_t pat[] = {0x48, 0x89, 0x05, 0, 0, 0, 0, 0xB0, 0x01, 0x48, 0x83, 0xC4, 0x28};
    const char mask[] = "xxx????xxxxxx";
    const auto hit = PatternScan(base, size, pat, mask);
    return hit ? ResolveRipMov(hit) : 0;
  }
  const std::uint8_t pat[] = {0x89, 0x35, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x66, 0x0F, 0xEF, 0xC0};
  const char mask[] = "xx????x????xxxx";
  const auto hit = PatternScan(base, size, pat, mask);
  if (!hit || !IsReadable(hit + 2, 4)) {
    return 0;
  }
  return static_cast<std::uintptr_t>(*reinterpret_cast<std::uint32_t*>(hit + 2));
}

bool EntryHasChrId(std::uintptr_t entry, int chr_id) {
  if (!entry || chr_id <= 0 || !IsReadable(entry, 0x40)) {
    return false;
  }
  // SotFS enemy param/chr id sits at +0x28 (DS2S-META). Probing extra offsets matched
  // unrelated int32s (e.g. Guardian Dragon 2120 while walking Majula→Heide).
  if (IsScholar()) {
    return ReadT<std::int32_t>(entry + 0x28) == chr_id;
  }
  constexpr int kChrOffs[] = {0x28, 0x14, 0x1C, 0x20, 0x24, 0x2C, 0x30, 0x10};
  for (int off : kChrOffs) {
    if (ReadT<std::int32_t>(entry + static_cast<std::uintptr_t>(off)) == chr_id) {
      return true;
    }
  }
  return false;
}

bool LoadedEnemyHasChr(std::uintptr_t table, int chr_id) {
  if (!table || chr_id <= 0) {
    return false;
  }
  const int stride = static_cast<int>(sizeof(std::uintptr_t));
  // Slot count kept modest — long scans amplified false pointer hits on vanilla.
  const int slots = IsScholar() ? 64 : 96;
  for (int i = 0; i < slots; ++i) {
    const auto entry = ReadPtr(table + static_cast<std::uintptr_t>(i * stride));
    if (!entry) {
      continue;
    }
    if (EntryHasChrId(entry, chr_id)) {
      return true;
    }
  }
  return false;
}

bool TryReadAnim(std::uintptr_t player_ctrl, std::int32_t* out_anim) {
  // Bob SotFS CT: Hero/Animation/Animation Data/Current Animation
  //   GameManagerImp → +D0 (PlayerCtrl) → +F8 → +38 → +78 → +20 → i32@+10
  // Older guesses kept as fallbacks.
  const int chains[][6] = {
      {0xF8, 0x38, 0x78, 0x20, 0x10, -1},
      {0xB8, 0x8, 0x20, 0xC, -1, -1},
      {0xB8, 0x8, 0x18, 0xC, -1, -1},
      {0xF8, 0x38, 0x78, 0x20, 0x0, -1},
  };
  for (const auto& ch : chains) {
    std::uintptr_t p = player_ctrl;
    bool ok = true;
    int last_off = -1;
    for (int i = 0; i < 6 && ch[i] >= 0; ++i) {
      last_off = ch[i];
      if (i + 1 < 6 && ch[i + 1] >= 0) {
        p = ReadPtr(p + static_cast<std::uintptr_t>(ch[i]));
        if (!p || !IsReadable(p, 8)) {
          ok = false;
          break;
        }
      }
    }
    if (!ok || last_off < 0) {
      continue;
    }
    const auto anim = ReadT<std::int32_t>(p + static_cast<std::uintptr_t>(last_off), -1);
    // DS2 Current Animation can be large Morpheme/TAE ids; keep a wide but sane band.
    if (anim >= 0 && anim < 2000000) {
      *out_anim = anim;
      return true;
    }
  }
  return false;
}

}  // namespace

bool GameState::ResolveBases() {
  if (!DetectFlavor()) {
    return false;
  }
  base_a_slot_ = ScanBaseASlot();
  if (!base_a_slot_) {
    LogWrite("game_state: GameManagerImp (BaseA) pattern not found");
    return false;
  }
  load_state_slot_ = ScanLoadStateSlot();
  if (!load_state_slot_) {
    LogWrite("game_state: LoadState pattern not found (load gating limited)");
  }

  boss_counters_ = 0;
  const auto& off = Offsets();
  if (off.boss_counters_chain_len > 0) {
    boss_counters_ = ResolveChain(base_a_slot_, off.boss_counters_chain, off.boss_counters_chain_len);
    if (boss_counters_) {
      // Final hop is a pointer to the kill-count array.
      boss_counters_ = ReadPtr(boss_counters_);
    }
    if (boss_counters_) {
      char bbuf[80];
      snprintf(bbuf, sizeof(bbuf), "game_state: boss_counters=0x%llX",
               static_cast<unsigned long long>(boss_counters_));
      LogWrite(bbuf);
    } else {
      LogWrite("game_state: boss_counters chain failed");
    }
  }

  char buf[220];
  snprintf(buf, sizeof(buf), "game_state: BaseA*=0x%llX LoadState*=0x%llX ver=%s",
           static_cast<unsigned long long>(base_a_slot_),
           static_cast<unsigned long long>(load_state_slot_), VersionName());
  LogWrite(buf);
  return true;
}

bool GameState::Init() {
  ready_ = ResolveBases();
  if (ready_) {
    if (!g_event_flags.Init()) {
      LogWrite("game_state: event flags unavailable (boss applause via kill counters if possible)");
    }
  }
  return ready_;
}

void GameState::Shutdown() {
  g_event_flags.Shutdown();
  ready_ = false;
  base_a_slot_ = 0;
  load_state_slot_ = 0;
  boss_counters_ = 0;
}

GameSnapshot GameState::Read() {
  GameSnapshot s{};
  if (!ready_ || !base_a_slot_) {
    return s;
  }

  const auto& off = Offsets();
  const auto base_a = ReadPtr(base_a_slot_);
  if (!base_a) {
    return s;
  }

  s.game_state = ReadT<std::int32_t>(base_a + static_cast<std::uintptr_t>(off.game_state_from_base_a),
                                    -1);
  s.on_title_screen = (s.game_state == kGameStateMainMenu);

  if (load_state_slot_) {
    std::uintptr_t ls = ReadPtr(load_state_slot_);
    if (ls && IsVanilla()) {
      ls = ReadPtr(ls);
    }
    if (ls) {
      s.is_loading =
          ReadT<std::int32_t>(ls + static_cast<std::uintptr_t>(off.load_state_flag_off)) == 1;
    }
  }

  const auto player_ctrl =
      ReadPtr(base_a + static_cast<std::uintptr_t>(off.player_ctrl_from_base_a));
  if (player_ctrl) {
    s.player_hp = ReadT<std::int32_t>(player_ctrl + static_cast<std::uintptr_t>(off.hp_from_player_ctrl));
    s.player_max_hp =
        ReadT<std::int32_t>(player_ctrl + static_cast<std::uintptr_t>(off.hp_max_from_player_ctrl));
    s.player_valid = (s.player_max_hp > 0 && s.player_max_hp <= 99999 && s.player_hp >= 0 &&
                      s.player_hp <= 99999);

    const auto player_param =
        ReadPtr(player_ctrl + static_cast<std::uintptr_t>(off.player_param_from_player_ctrl));
    if (player_param) {
      s.deaths = ReadT<std::int32_t>(
          player_param + static_cast<std::uintptr_t>(off.deaths_from_player_param));
    }

    std::int32_t anim = -1;
    if (TryReadAnim(player_ctrl, &anim)) {
      s.current_anim = anim;
      s.anim_valid = true;
    }
  }

  // Wipe signal: current map id, preferring anything that is NOT LastSetBonfireAreaID
  // (Betwixt→Majula is seamless — bonfire area stays Betwixt until you rest).
  {
    std::int32_t map_id = 0;
    int map_tag = -1;
    if (TryReadCurrentMapId(base_a, off, &map_id, &map_tag)) {
      s.area_id = map_id;
      s.area_valid = true;
      // tag < 0 ⇒ bonfire fallback only (not safe for seamless wipe).
      s.area_is_map_manager = (map_tag >= 0);
      MaybeLogMapProbe(0, map_id, map_tag);
    } else if (s.player_valid) {
      MaybeLogAreaMiss(base_a, off);
    }
  }

  s.in_gameplay = s.player_valid && !s.is_loading && s.game_state == kGameStateInGame;

  // EventManager / boss kill array are often null on the title screen — retry in-game.
  if (s.player_valid) {
    if (!g_event_flags.Ready()) {
      g_event_flags.EnsureReady();
    }
    if (!boss_counters_ && off.boss_counters_chain_len > 0) {
      boss_counters_ =
          ResolveChain(base_a_slot_, off.boss_counters_chain, off.boss_counters_chain_len);
      if (boss_counters_) {
        boss_counters_ = ReadPtr(boss_counters_);
      }
      if (boss_counters_) {
        char bbuf[80];
        snprintf(bbuf, sizeof(bbuf), "game_state: boss_counters=0x%llX (late)",
                 static_cast<unsigned long long>(boss_counters_));
        LogWrite(bbuf);
      }
    }
  }

  std::uintptr_t enemies = 0;
  if (off.loaded_enemies_from_base_a >= 0) {
    enemies = ReadPtr(base_a + static_cast<std::uintptr_t>(off.loaded_enemies_from_base_a));
  }

  const bool flags_ok = g_event_flags.Ready();
  if (s.in_gameplay) {
    std::unordered_set<std::int32_t> defeat_seen;
    std::unordered_set<std::int32_t> kill_seen;
    for (const auto& boss : kBosses) {
      bool defeated = false;
      if (flags_ok && g_event_flags.ReadFlag(boss.defeat_flag, &defeated) && defeated) {
        if (defeat_seen.insert(boss.defeat_flag).second) {
          s.defeat_flags_on.push_back(boss.defeat_flag);
        }
      }

      if (boss_counters_ && boss.boss_type_off >= 0) {
        const auto kills =
            ReadT<std::int32_t>(boss_counters_ + static_cast<std::uintptr_t>(boss.boss_type_off));
        if (kills > 0 && kill_seen.insert(boss.boss_type_off).second) {
          s.boss_kills_on.push_back(boss.boss_type_off);
        }
      }

      // Cheer gating uses defeat *flags* only (kill counts stay set after ascetics).
      bool cheer_defeated = defeated;
      if (!flags_ok) {
        cheer_defeated = false;
      }

      bool loaded = false;
      if (enemies && !cheer_defeated) {
        for (int k = 0; k < 4 && boss.chr_ids[k]; ++k) {
          if (LoadedEnemyHasChr(enemies, boss.chr_ids[k])) {
            loaded = true;
            s.cheer_chr_ids.push_back(boss.chr_ids[k]);
            s.cheer_defeat_flags.push_back(boss.defeat_flag);
            break;
          }
        }
      }
      if (loaded && !cheer_defeated && !s.boss_fight_active) {
        s.boss_fight_active = true;
        s.boss_defeat_flag = boss.defeat_flag;
      }
    }
  }

  return s;
}

}  // namespace sitcom
