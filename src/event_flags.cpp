#include "event_flags.h"

#include "log.h"

#include <windows.h>
#include <psapi.h>

#include <cstring>
#include <string>
#include <unordered_map>

namespace sitcom {
namespace {

bool IsReadable(std::uintptr_t ptr) {
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
  return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_EXECUTE_READ ||
         p == PAGE_EXECUTE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY;
}

std::uintptr_t PatternScan(std::uintptr_t base, std::size_t size, const std::uint8_t* pat,
                           const char* mask) {
  const std::size_t len = std::strlen(mask);
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

std::uintptr_t ResolveRip(std::uintptr_t insn) {
  const auto rel = *reinterpret_cast<std::int32_t*>(insn + 3);
  return insn + 7 + rel;
}

const std::unordered_map<std::string, int> kFlagGroups = {
    {"0", 0x00000}, {"1", 0x00500}, {"5", 0x05F00}, {"6", 0x0B900}, {"7", 0x11300},
};

const std::unordered_map<std::string, int> kFlagAreas = {
    {"000", 0},  {"100", 1},  {"101", 2},  {"102", 3},  {"110", 4},  {"120", 5},
    {"121", 6},  {"130", 7},  {"131", 8},  {"132", 9},  {"140", 10}, {"141", 11},
    {"150", 12}, {"151", 13}, {"160", 14}, {"170", 15}, {"180", 16}, {"181", 17},
};

}  // namespace

bool EventFlags::FlagOffset(std::int32_t flag_id, int* out_offset, std::uint32_t* out_mask) {
  // DSR-Gadget / EventPocket algorithm (IDs zero-padded to 8 digits).
  char id[16];
  snprintf(id, sizeof(id), "%08d", flag_id);
  if (std::strlen(id) != 8) {
    return false;
  }
  const std::string group(id, 1);
  const std::string area(id + 1, 3);
  const int section = id[4] - '0';
  const int number = (id[5] - '0') * 100 + (id[6] - '0') * 10 + (id[7] - '0');

  const auto g = kFlagGroups.find(group);
  const auto a = kFlagAreas.find(area);
  if (g == kFlagGroups.end() || a == kFlagAreas.end()) {
    return false;
  }

  int offset = g->second;
  offset += a->second * 0x500;
  offset += section * 128;
  offset += (number - (number % 32)) / 8;
  *out_offset = offset;
  *out_mask = 0x80000000u >> (number % 32);
  return true;
}

bool EventFlags::Resolve() {
  HMODULE game = GetModuleHandleW(L"DarkSoulsRemastered.exe");
  if (!game) {
    return false;
  }
  MODULEINFO info{};
  if (!GetModuleInformation(GetCurrentProcess(), game, &info, sizeof(info))) {
    return false;
  }
  const auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
  const auto size = static_cast<std::size_t>(info.SizeOfImage);

  // EventFlagsAOB from DSR-Gadget / EventPocket
  // 48 8B 0D ?? ?? ?? ?? 99 33 C2 45 33 C0 2B C2 8D 50 F6
  const std::uint8_t pat[] = {0x48, 0x8B, 0x0D, 0, 0, 0, 0, 0x99, 0x33, 0xC2, 0x45, 0x33,
                              0xC0, 0x2B, 0xC2, 0x8D, 0x50, 0xF6};
  const char mask[] = "xxx????xxxxxxxxxxx";
  const auto hit = PatternScan(base, size, pat, mask);
  if (!hit) {
    LogWrite("event_flags: AOB not found");
    return false;
  }
  flags_ptr_slot_ = ResolveRip(hit);
  char buf[80];
  snprintf(buf, sizeof(buf), "event_flags: slot=0x%llX",
           static_cast<unsigned long long>(flags_ptr_slot_));
  LogWrite(buf);
  return flags_ptr_slot_ != 0;
}

bool EventFlags::Init() {
  ready_ = Resolve();
  return ready_;
}

void EventFlags::Shutdown() {
  ready_ = false;
  flags_ptr_slot_ = 0;
}

bool EventFlags::ReadFlag(std::int32_t flag_id, bool* out_value) const {
  if (!ready_ || !out_value) {
    return false;
  }
  int offset = 0;
  std::uint32_t mask = 0;
  if (!FlagOffset(flag_id, &offset, &mask)) {
    return false;
  }
  if (!IsReadable(flags_ptr_slot_)) {
    return false;
  }

  // EventPocket / DSR-Gadget: [[slot] + 0] + 0  — two pointer hops, then bitfield + offset.
  const auto level1 = *reinterpret_cast<std::uintptr_t*>(flags_ptr_slot_);
  if (!level1 || !IsReadable(level1)) {
    return false;
  }
  const auto bitfield = *reinterpret_cast<std::uintptr_t*>(level1);
  if (!bitfield || !IsReadable(bitfield + static_cast<std::uintptr_t>(offset))) {
    return false;
  }

  const auto word = *reinterpret_cast<std::uint32_t*>(bitfield + static_cast<std::uintptr_t>(offset));
  *out_value = (word & mask) != 0;
  return true;
}

}  // namespace sitcom
