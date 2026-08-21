#pragma once

#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace sitcom {

enum class Ds2Edition { Unknown, Scholar, Vanilla };

enum class Ds2Version {
  Unhooked,
  Unsupported,
  Sotfs102,
  Sotfs103,
  Vanilla102,
  Vanilla111,
  Vanilla112,
};

// DS2S-META GAMESTATE leaf (GameManagerImp).
constexpr std::int32_t kGameStateInGame = 0x1e;
constexpr std::int32_t kGameStateMainMenu = 0x0a;

struct Ds2Offsets {
  int player_ctrl_from_base_a = 0;
  int hp_from_player_ctrl = 0;
  int hp_max_from_player_ctrl = 0;
  int player_param_from_player_ctrl = 0;
  int deaths_from_player_param = 0;
  int game_state_from_base_a = 0;
  int event_manager_from_base_a = 0;
  int last_bonfire_area_from_event_manager = 0;
  int loaded_enemies_from_base_a = -1;  // -1 = unavailable
  int map_manager_from_base_a = -1;
  // LoadState AOB extra: int32 at resolved pointer + this == 1 when loading.
  int load_state_flag_off = 0;
  // SoulMemory boss kill-count array (from GameManagerImp slot). -1 = unused.
  int boss_counters_chain[6] = {-1, -1, -1, -1, -1, -1};
  int boss_counters_chain_len = 0;
};

bool DetectFlavor();
Ds2Edition Edition();
Ds2Version Version();
bool IsScholar();
bool IsVanilla();
const Ds2Offsets& Offsets();
HMODULE GameModule();
std::uintptr_t GameBase();
std::size_t GameSize();
const char* VersionName();

}  // namespace sitcom
