#pragma once

#include "audio.h"
#include "config.h"
#include "game_state.h"

#include <cstdint>

namespace sitcom {

class EventDetector {
 public:
  void Reset();
  void Update(const GameSnapshot& snap, const Config& cfg, Audio& audio);

 private:
  bool has_prev_ = false;
  GameSnapshot prev_{};
  std::uint64_t last_laugh_hit_ms_ = 0;
  std::uint64_t last_ooh_ms_ = 0;
  std::uint64_t last_fail_laugh_ms_ = 0;
  bool flags_seeded_ = false;
  std::uint64_t gameplay_seeded_ms_ = 0;
  // Last map id seen in real gameplay (not loading). Survives load screens so
  // map changes that happen mid-load still produce a wipe on the next gameplay frame.
  std::int32_t stable_area_id_ = 0;
  // Fallback wipe: loading → gameplay with unchanged death count (skips death reload).
  bool load_edge_armed_ = false;
  std::int32_t deaths_at_load_start_ = 0;
  std::uint64_t last_wipe_ms_ = 0;
};

}  // namespace sitcom
