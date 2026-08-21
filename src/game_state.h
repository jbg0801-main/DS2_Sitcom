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
  std::vector<std::int32_t> cheer_chr_ids;

  bool area_valid = false;
  std::int32_t area_id = 0;

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
  bool ready_ = false;
};

}  // namespace sitcom
