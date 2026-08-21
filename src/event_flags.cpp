#include "event_flags.h"

#include "ds2_flavor.h"
#include "log.h"
#include "mem.h"

#include <cstdio>
#include <string>

namespace sitcom {
namespace {

std::uintptr_t ScanGameManagerImpSlot() {
  const auto base = GameBase();
  const auto size = GameSize();
  if (!base || !size) {
    return 0;
  }

  if (IsScholar()) {
    const std::uint8_t pat_a[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x48, 0x8B, 0x58, 0x38,
                                  0x48, 0x85, 0xDB, 0x74, 0,    0xF6};
    const char mask_a[] = "xxx????xxxxxxxx?x";
    auto hit = PatternScan(base, size, pat_a, mask_a);
    if (!hit) {
      const std::uint8_t pat_b[] = {0x48, 0x8B, 0x35, 0, 0, 0, 0, 0x48, 0x8B, 0xE9, 0x48, 0x85,
                                    0xF6};
      const char mask_b[] = "xxx????xxxxxx";
      hit = PatternScan(base, size, pat_b, mask_b);
    }
    if (!hit) {
      return 0;
    }
    return ResolveRipMov(hit);
  }

  const std::uint8_t pat[] = {0x8B, 0xF1, 0x8B, 0x0D, 0,    0,    0,    0,    0x8B, 0x01, 0x8B,
                             0x50, 0x28, 0xFF, 0xD2, 0x84, 0xC0, 0x74, 0x0C};
  const char mask[] = "xxxx????xxxxxxxxxxx";
  const auto hit = PatternScan(base, size, pat, mask);
  if (!hit || !IsReadable(hit + 4, 4)) {
    return 0;
  }
  return static_cast<std::uintptr_t>(*reinterpret_cast<std::uint32_t*>(hit + 4));
}

bool ReadFlagScholar(std::uintptr_t flags_base, std::uint32_t event_flag_id, bool* out) {
  const auto event_category = (event_flag_id / 10000u) * 0x89u;
  const auto u_var1 =
      (((event_category - event_category / 0x1Fu) >> 1) + event_category / 0x1Fu) >> 4;
  const auto r8d = event_category - u_var1 * 31u;
  const auto bucket = static_cast<int>(r8d * 8u + 0x20u);

  std::uintptr_t vector = ReadPtr(flags_base + static_cast<std::uintptr_t>(bucket));
  for (int i = 0; i < 100; ++i) {
    if (!vector) {
      return false;
    }
    const auto group = ReadT<std::int32_t>(vector + 0xC, -1);
    if (group == static_cast<std::int32_t>(event_flag_id / 10000u)) {
      const auto category2 = (event_flag_id % 10000u) >> 3;
      const auto limit = ReadT<std::uint32_t>(vector + 0x8);
      if (category2 < limit) {
        const auto bitfield = ReadPtr(vector);
        if (!bitfield) {
          return false;
        }
        const auto flag_bit = ReadT<std::uint8_t>(bitfield + category2);
        const auto shift = static_cast<int>(0x7 - (event_flag_id % 10000u & 0x7));
        const auto mask = static_cast<std::uint8_t>(1u << shift);
        *out = (flag_bit & mask) != 0;
        return true;
      }
      return false;
    }
    vector = ReadPtr(vector + 0x10);
  }
  return false;
}

bool ReadFlagVanilla(std::uintptr_t flags_base, std::uint32_t event_flag_id, bool* out) {
  const auto event_category = (event_flag_id / 10000u) * 0x89u;
  const auto bucket = static_cast<int>((event_category % 0x1Fu) * 4u + 0x10u);

  std::uintptr_t vector = ReadPtr(flags_base + static_cast<std::uintptr_t>(bucket));
  for (int i = 0; i < 100; ++i) {
    if (!vector) {
      return false;
    }
    const auto group = ReadT<std::int32_t>(vector + 0x8, -1);
    if (group == static_cast<std::int32_t>(event_flag_id / 10000u)) {
      const auto category2 = (event_flag_id % 10000u) >> 3;
      const auto limit = ReadT<std::uint32_t>(vector + 0x4);
      if (category2 < limit) {
        const auto bitfield = ReadPtr(vector);
        if (!bitfield) {
          return false;
        }
        const auto flag_bit = ReadT<std::uint8_t>(bitfield + category2);
        const auto shift = static_cast<int>(0x7 - (event_flag_id % 10000u & 0x7));
        const auto mask = static_cast<std::uint8_t>(1u << shift);
        *out = (flag_bit & mask) != 0;
        return true;
      }
      return false;
    }
    vector = ReadPtr(vector + 0xC);
  }
  return false;
}

}  // namespace

bool EventFlags::Resolve() {
  const auto slot = ScanGameManagerImpSlot();
  if (!slot) {
    LogWrite("event_flags: GameManagerImp AOB not found");
    return false;
  }

  if (IsScholar()) {
    const int chain[] = {0, 0x70, 0x20};
    flags_base_ = ResolveChain(slot, chain, 3);
  } else {
    const int chain[] = {0, 0, 0x44, 0x10};
    flags_base_ = ResolveChain(slot, chain, 4);
  }

  if (!flags_base_) {
    LogWrite("event_flags: EventManager chain failed");
    return false;
  }

  char buf[80];
  snprintf(buf, sizeof(buf), "event_flags: base=0x%llX (%s)",
           static_cast<unsigned long long>(flags_base_), IsScholar() ? "scholar" : "vanilla");
  LogWrite(buf);
  return true;
}

bool EventFlags::Init() {
  ready_ = Resolve();
  return ready_;
}

void EventFlags::Shutdown() {
  ready_ = false;
  flags_base_ = 0;
}

bool EventFlags::ReadFlag(std::int32_t flag_id, bool* out_value) const {
  if (!ready_ || !out_value || flag_id <= 0) {
    return false;
  }
  if (IsScholar()) {
    return ReadFlagScholar(flags_base_, static_cast<std::uint32_t>(flag_id), out_value);
  }
  return ReadFlagVanilla(flags_base_, static_cast<std::uint32_t>(flag_id), out_value);
}

}  // namespace sitcom
