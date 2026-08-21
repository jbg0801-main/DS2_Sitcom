#pragma once

#include <cstdint>
#include <vector>

namespace sitcom {

// MenuMan+0x104 "Message" / Display Banner type (DSR-Gadget / RemasterCETable).
constexpr std::uint8_t kBannerNone = 0;
constexpr std::uint8_t kBannerCurrentLocation = 8;  // area title card ("Map Name")
constexpr std::uint8_t kBannerBonfireLit = 13;

// Boss defeat flags used for special SFX.
constexpr std::int32_t kDefeatSif = 5;
constexpr std::int32_t kDefeatPinwheel = 6;
constexpr std::int32_t kDefeatBoC = 10;
constexpr std::int32_t kDefeatOS = 12;
constexpr std::int32_t kCheerPinwheel = 11305392;
constexpr std::int32_t kCheerBoC = 11415392;
constexpr std::int32_t kCheerOS = 11515392;

// Chr model id string "c2360" → Smough (RemasterCETable).
constexpr int kModelSmough = 2360;

struct GameSnapshot {
  bool player_valid = false;
  bool in_gameplay = false;
  std::int32_t player_hp = 0;
  std::int32_t player_max_hp = 0;
  std::int32_t deaths = 0;

  bool boss_fight_active = false;
  std::int32_t boss_defeat_flag = 0;
  std::int32_t boss_cheer_flag = 0;

  std::vector<std::int32_t> defeat_flags_on;
  std::vector<std::int32_t> cheer_flags_on;

  bool area_valid = false;
  std::uint8_t area_number = 0;
  std::uint8_t world_number = 0;

  bool banner_valid = false;
  std::uint8_t banner_message = 0;

  bool on_title_screen = false;

  // Lock-on (best-effort). lock_model_id == kModelSmough when locked onto Smough.
  bool lock_on = false;
  bool lock_model_valid = false;
  int lock_model_id = 0;

  // Estus quick count when readable (for empty-flask heuristic). -1 = unknown.
  std::int32_t estus_count = -1;

  // DSR-Gadget player anims (WorldChr → ChrData1 chains). -1 = unread.
  std::int32_t current_anim = -1;
  std::int32_t stay_anim_upper = -1;
  std::int32_t stay_anim_lower = -1;
  bool anim_valid = false;
};

class GameState {
 public:
  bool Init();
  void Shutdown();
  GameSnapshot Read();

 private:
  bool ResolveBases();

  std::uintptr_t base_b_ptr_ = 0;
  std::uintptr_t base_e_ptr_ = 0;
  std::uintptr_t menu_man_ptr_ = 0;
  std::uintptr_t world_chr_ptr_ = 0;  // WorldChrBase slot
  std::uintptr_t lock_tgt_ptr_ = 0;   // LockTGTBase slot
  bool ready_ = false;
};

}  // namespace sitcom
