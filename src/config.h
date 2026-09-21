#pragma once

#include <string>

namespace sitcom {

struct Config {
  bool enabled = true;
  bool log = false;
  // Proton-friendly PlaceName diagnosis: extra UI hooks + field polls → sitcom.log
  bool trace_area_title = false;
  int poll_hz = 20;
  float volume = 0.7f;

  bool laugh_on_hit = true;
  bool laugh_on_death = true;
  bool cheer_on_boss_bar = true;
  bool applause_on_boss_death = true;
  bool wipe_on_area_title = true;
  bool ooh_on_item_get = true;

  bool laugh_on_empty_flask = true;
  bool laugh_on_locked_door = true;
  bool laugh_on_no_spell = true;
  bool laugh_on_ladder_fall = true;

  float laugh_hit_seconds = 1.5f;
  float ooh_cooldown_seconds = 0.75f;

  std::string sounds_dir = "sounds";
};

bool LoadConfig(const std::wstring& ini_path, Config& out);

}  // namespace sitcom
