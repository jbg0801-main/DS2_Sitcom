#include "mem.h"

#include <cstring>

namespace sitcom {

bool IsReadable(std::uintptr_t ptr, std::size_t n) {
  if (!ptr) {
    return false;
  }
  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(reinterpret_cast<LPCVOID>(ptr), &mbi, sizeof(mbi))) {
    return false;
  }
  if (mbi.State != MEM_COMMIT) {
    return false;
  }
  const DWORD p = mbi.Protect & 0xFF;
  if (p == PAGE_NOACCESS || p == PAGE_EXECUTE) {
    return false;
  }
  const auto start = reinterpret_cast<const std::uint8_t*>(ptr);
  const auto end = start + n;
  const auto reg_start = static_cast<const std::uint8_t*>(mbi.BaseAddress);
  const auto reg_end = reg_start + mbi.RegionSize;
  return start >= reg_start && end <= reg_end;
}

bool IsExecutable(const void* p) {
  if (!p) {
    return false;
  }
  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(p, &mbi, sizeof(mbi))) {
    return false;
  }
  if (mbi.State != MEM_COMMIT) {
    return false;
  }
  const DWORD prot = mbi.Protect & 0xFF;
  return prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
         prot == PAGE_EXECUTE_WRITECOPY;
}

std::uintptr_t PatternScan(std::uintptr_t base, std::size_t size, const std::uint8_t* pat,
                           const char* mask) {
  const std::size_t len = std::strlen(mask);
  if (len == 0 || size < len) {
    return 0;
  }
  for (std::size_t i = 0; i + len <= size; ++i) {
    bool ok = true;
    for (std::size_t j = 0; j < len; ++j) {
      if (mask[j] == 'x' && *reinterpret_cast<const std::uint8_t*>(base + i + j) != pat[j]) {
        ok = false;
        break;
      }
    }
    if (ok) {
      return base + i;
    }
  }
  return 0;
}

std::uintptr_t ResolveRipMov(std::uintptr_t insn) {
  if (!IsReadable(insn + 3, 4)) {
    return 0;
  }
  const auto rel = *reinterpret_cast<std::int32_t*>(insn + 3);
  return insn + 7 + rel;
}

std::uintptr_t ResolveChain(std::uintptr_t slot, const int* offsets, int count) {
  if (!slot || count <= 0) {
    return 0;
  }
  std::uintptr_t ptr = slot;
  for (int i = 0; i < count; ++i) {
    const auto address = ptr + static_cast<std::uintptr_t>(offsets[i]);
    if (i + 1 < count) {
      ptr = ReadPtr(address);
      if (!ptr) {
        return 0;
      }
    } else {
      ptr = address;
    }
  }
  return ptr;
}

}  // namespace sitcom
