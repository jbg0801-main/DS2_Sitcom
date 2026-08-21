#pragma once

#include <cstdint>
#include <cstddef>
#include <windows.h>

namespace sitcom {

bool IsReadable(std::uintptr_t ptr, std::size_t n = sizeof(std::uintptr_t));
bool IsExecutable(const void* p);

std::uintptr_t PatternScan(std::uintptr_t base, std::size_t size, const std::uint8_t* pat,
                           const char* mask);

// x64: RIP-relative [rip+disp32] at insn+3, instruction length 7.
std::uintptr_t ResolveRipMov(std::uintptr_t insn);

template <typename T>
T ReadT(std::uintptr_t ptr, T fallback = T{}) {
  return IsReadable(ptr, sizeof(T)) ? *reinterpret_cast<T*>(ptr) : fallback;
}

inline std::uintptr_t ReadPtr(std::uintptr_t ptr) {
  return ReadT<std::uintptr_t>(ptr, 0);
}

// Resolve a pointer chain. Every offset except the last is dereferenced
// (SoulMemory / PropertyHook convention).
std::uintptr_t ResolveChain(std::uintptr_t slot, const int* offsets, int count);

}  // namespace sitcom
