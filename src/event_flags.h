#pragma once

#include <cstdint>

namespace sitcom {

class EventFlags {
 public:
  bool Init();
  void Shutdown();
  bool Ready() const { return ready_; }
  bool ReadFlag(std::int32_t flag_id, bool* out_value) const;

 private:
  bool Resolve();

  std::uintptr_t flags_base_ = 0;  // resolved EventFlagManager (SoulMemory chain end)
  bool ready_ = false;
};

}  // namespace sitcom
