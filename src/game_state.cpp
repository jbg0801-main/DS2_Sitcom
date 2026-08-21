#include "game_state.h"

#include "ds2_flavor.h"
#include "event_flags.h"
#include "log.h"
#include "mem.h"

#include <cstdio>
#include <string>
#include <unordered_set>

namespace sitcom {
namespace {

EventFlags g_event_flags;

struct BossDef {
  std::int32_t defeat_flag;
  int chr_ids[4];
};

// Defeat flags: SoulSplitter wiki. Chr IDs: soulsmodding cXXXX (numeric).
constexpr BossDef kBosses[] = {
    {100971, {3096, 0, 0, 0}},          // Last Giant
    {100968, {3180, 0, 0, 0}},          // Pursuer
    {100953, {6191, 0, 0, 0}},          // Executioner's Chariot
    {100958, {5040, 0, 0, 0}},          // Looking Glass Knight
    {100954, {1540, 0, 0, 0}},          // Skeleton Lords
    {100961, {3033, 0, 0, 0}},          // Flexile Sentry
    {100963, {6260, 0, 0, 0}},          // Lost Sinner
    {101001, {3240, 0, 0, 0}},          // Belfry Gargoyles
    {100962, {3250, 0, 0, 0}},          // Ruin Sentinels
    {100965, {2261, 0, 0, 0}},          // Royal Rat Vanguard
    {100967, {6280, 0, 0, 0}},          // Royal Rat Authority
    {100957, {5030, 0, 0, 0}},          // Scorpioness Najka
    {100951, {6030, 0, 0, 0}},          // Duke's Dear Freja
    {100956, {5010, 0, 0, 0}},          // Mytha
    {100966, {3260, 0, 0, 0}},          // The Rotten
    {100960, {6250, 0, 0, 0}},          // Old Dragonslayer
    {100955, {5000, 5001, 0, 0}},       // Covetous Demon
    {100964, {3050, 0, 0, 0}},          // Smelter Demon (red)
    {100952, {6070, 0, 0, 0}},          // Old Iron King
    {100970, {2120, 0, 0, 0}},          // Guardian Dragon
    {100950, {6020, 0, 0, 0}},          // Demon of Song
    {100975, {3330, 0, 0, 0}},          // Velstadt
    {100978, {5146, 6840, 0, 0}},       // Vendrick
    {100979, {5061, 0, 0, 0}},          // Darklurker
    {100959, {6110, 6115, 0, 0}},       // Dragonrider
    {100980, {6110, 6115, 0, 0}},       // Twin Dragonriders
    {101000, {1370, 0, 0, 0}},          // Prowling Magus
    {100972, {3097, 0, 0, 0}},          // Giant Lord
    {100977, {6000, 0, 0, 0}},          // Ancient Dragon
    {100974, {3320, 3340, 0, 0}},       // Throne Watcher & Defender
    {100973, {6270, 7036, 0, 0}},       // Nashandra
    {101080, {6920, 0, 0, 0}},          // Aldia
    {101050, {6820, 0, 0, 0}},          // Elana
    {101051, {6810, 0, 0, 0}},          // Sinh
    {101053, {0, 0, 0, 0}},             // Graverobber trio (applause via flag only)
    {101063, {3052, 0, 0, 0}},          // Blue Smelter
    {101061, {6750, 0, 0, 0}},          // Fume Knight
    {101062, {6800, 0, 0, 0}},          // Sir Alonne
    {101070, {6900, 0, 0, 0}},          // Burnt Ivory King
    {101071, {6791, 0, 0, 0}},          // Aava
    {101072, {6790, 0, 0, 0}},          // Lud & Zallen
};

std::uintptr_t ScanBaseASlot() {
  const auto base = GameBase();
  const auto size = GameSize();
  if (!base) {
    return 0;
  }
  if (IsScholar()) {
    const std::uint8_t pat_a[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x48, 0x8B, 0x58, 0x38,
                                  0x48, 0x85, 0xDB, 0x74, 0,    0xF6};
    const char mask_a[] = "xxx????xxxxxxxx?x";
    auto hit = PatternScan(base, size, pat_a, mask_a);
    if (!hit) {
      const std::uint8_t pat_b[] = {0x48, 0x8B, 0x35, 0, 0, 0, 0, 0x48, 0x8B, 0xE9, 0x48, 0x85,
                                    0xF6};
      const char mask_b[] = "xxx????xxxxxx";
      hit = PatternScan(base, size, pat_b, mask_b);
    }
    return hit ? ResolveRipMov(hit) : 0;
  }
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

bool LoadedEnemyHasChr(std::uintptr_t table, int chr_id) {
  if (!table || chr_id <= 0) {
    return false;
  }
  const int stride = static_cast<int>(sizeof(std::uintptr_t));
  for (int i = 0; i < 70; ++i) {
    const auto entry = ReadPtr(table + static_cast<std::uintptr_t>(i * stride));
    if (!entry) {
      continue;
    }
    if (ReadT<std::int32_t>(entry + 0x28) == chr_id) {
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
      LogWrite("game_state: event flags unavailable (boss applause/cheer limited)");
    }
  }
  return ready_;
}

void GameState::Shutdown() {
  g_event_flags.Shutdown();
  ready_ = false;
  base_a_slot_ = 0;
  load_state_slot_ = 0;
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

    // Best-effort current anim (CE-confirm). Typical PlayerCtrl+0xB8 chain on SotFS.
    if (IsScholar()) {
      const auto a = ReadPtr(player_ctrl + 0xB8);
      const auto b = a ? ReadPtr(a + 0x8) : 0;
      const auto c = b ? ReadPtr(b + 0x20) : 0;
      if (c) {
        const auto anim = ReadT<std::int32_t>(c + 0xC, -1);
        if (anim >= 0 && anim < 20000) {
          s.current_anim = anim;
          s.anim_valid = true;
        }
      }
    }
  }

  const auto event_man =
      ReadPtr(base_a + static_cast<std::uintptr_t>(off.event_manager_from_base_a));
  if (event_man) {
    s.area_id = ReadT<std::int32_t>(
        event_man + static_cast<std::uintptr_t>(off.last_bonfire_area_from_event_manager));
    s.area_valid = (s.area_id != 0);
  }
  if (off.map_manager_from_base_a >= 0) {
    const auto map_man =
        ReadPtr(base_a + static_cast<std::uintptr_t>(off.map_manager_from_base_a));
    if (map_man) {
      const auto map_id = ReadT<std::int32_t>(map_man + 0x8);
      if (map_id != 0) {
        s.area_id = map_id;
        s.area_valid = true;
      }
    }
  }

  s.in_gameplay = s.player_valid && !s.is_loading && s.game_state == kGameStateInGame;

  std::uintptr_t enemies = 0;
  if (off.loaded_enemies_from_base_a >= 0) {
    enemies = ReadPtr(base_a + static_cast<std::uintptr_t>(off.loaded_enemies_from_base_a));
  }

  if (g_event_flags.Ready() && s.in_gameplay) {
    std::unordered_set<std::int32_t> defeat_seen;
    for (const auto& boss : kBosses) {
      bool defeated = false;
      if (!g_event_flags.ReadFlag(boss.defeat_flag, &defeated)) {
        continue;
      }
      if (defeated && defeat_seen.insert(boss.defeat_flag).second) {
        s.defeat_flags_on.push_back(boss.defeat_flag);
      }

      bool loaded = false;
      if (enemies && !defeated) {
        for (int k = 0; k < 4 && boss.chr_ids[k]; ++k) {
          if (LoadedEnemyHasChr(enemies, boss.chr_ids[k])) {
            loaded = true;
            s.cheer_chr_ids.push_back(boss.chr_ids[k]);
            break;
          }
        }
      }
      if (loaded && !defeated && !s.boss_fight_active) {
        s.boss_fight_active = true;
        s.boss_defeat_flag = boss.defeat_flag;
      }
    }
  }

  return s;
}

}  // namespace sitcom
