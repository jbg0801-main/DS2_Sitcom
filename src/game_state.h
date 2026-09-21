#pragma once

#include <cstdint>
#include <vector>

namespace sitcom {

struct GameSnapshot {
  bool player_valid = false;
  bool in_gameplay = false;
  bool is_loading = false;
  std::int32_t player_hp = 0;
  std::int32_t player_max_hp = 0;
  std::int32_t deaths = 0;
  std::int32_t game_state = -1;

  bool boss_fight_active = false;
  std::int32_t boss_defeat_flag = 0;

  std::vector<std::int32_t> defeat_flags_on;
  // Parallel: loaded boss chr id + that boss's defeat flag (same index).
  std::vector<std::int32_t> cheer_chr_ids;
  std::vector<std::int32_t> cheer_defeat_flags;
  // SoulMemory BossType kill counts that are > 0 (for applause rising-edge backup).
  std::vector<std::int32_t> boss_kills_on;

  // Map / zone id suitable for area-title wipe (NOT last-bonfire area).
  bool area_valid = false;
  std::int32_t area_id = 0;
  bool area_is_map_manager = false;

  bool on_title_screen = false;

  std::int32_t current_anim = -1;
  bool anim_valid = false;
};

class GameState {
 public:
  bool Init();
  void Shutdown();
  GameSnapshot Read();

 private:
  bool ResolveBases();

  std::uintptr_t base_a_slot_ = 0;
  std::uintptr_t load_state_slot_ = 0;
  std::uintptr_t boss_counters_ = 0;
  bool ready_ = false;
};

}  // namespace sitcom
