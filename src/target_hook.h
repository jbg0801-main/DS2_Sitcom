#pragma once

#include <cstdint>

namespace sitcom {

// Optional trampoline that captures the last locked-on enemy ChrIns pointer.
bool InstallTargetHook();
void RemoveTargetHook();
std::uintptr_t GetLockedTargetPtr();

}  // namespace sitcom
