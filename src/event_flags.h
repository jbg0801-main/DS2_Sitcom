#pragma once

#include <cstdint>

namespace sitcom {

class EventFlags {
 public:
  bool Init();
  void Shutdown();
  bool Ready() const { return ready_; }
  // Returns false if unreadble / unknown ID encoding.
  bool ReadFlag(std::int32_t flag_id, bool* out_value) const;

 private:
  bool Resolve();
  static bool FlagOffset(std::int32_t flag_id, int* out_offset, std::uint32_t* out_mask);

  std::uintptr_t flags_ptr_slot_ = 0;  // RIP slot → EventFlags base
  bool ready_ = false;
};

}  // namespace sitcom
