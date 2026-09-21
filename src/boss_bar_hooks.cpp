#include "boss_bar_hooks.h"

#include "ds2_flavor.h"
#include "log.h"
#include "mem.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sitcom {
namespace {

// SotFS 1.03 FeSceneBossHpGuage (FromSoft spelling). Do not hook EnemyHpGuage.
constexpr std::uintptr_t kVtRva = 0x10B0C68;
constexpr std::uintptr_t kVtSlot4Rva = 0x10B0C88;
constexpr std::uintptr_t kTickRva = 0x60F00;

std::atomic<bool> g_ready{false};
std::atomic<bool> g_trace{false};
std::atomic<void*> g_boss_bar{nullptr};

using FnThis = void(__cdecl*)(void* self);
void** g_slot_tick = nullptr;
FnThis g_real_tick = nullptr;

struct TraceSnap {
  std::int32_t d0 = 0;
  std::int32_t b8 = 0;
  std::int32_t bc = 0;
  std::int32_t c0 = 0;
  std::int32_t c4 = 0;
  std::int32_t dc = 0;
  std::uint16_t ce = 0;
  std::uint8_t d5 = 0;
  bool operator==(const TraceSnap& o) const {
    return d0 == o.d0 && b8 == o.b8 && bc == o.bc && c0 == o.c0 && c4 == o.c4 && dc == o.dc &&
           ce == o.ce && d5 == o.d5;
  }
};

TraceSnap g_last_trace{};
std::uint64_t g_last_scan_ms = 0;

std::int32_t ReadI32(void* obj, std::uint32_t off) {
  if (!obj || !IsReadable(reinterpret_cast<std::uintptr_t>(obj) + off, 4)) {
    return 0;
  }
  return *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(obj) + off);
}

std::uint16_t ReadU16(void* obj, std::uint32_t off) {
  if (!obj || !IsReadable(reinterpret_cast<std::uintptr_t>(obj) + off, 2)) {
    return 0;
  }
  return *reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(obj) + off);
}

std::uint8_t ReadU8(void* obj, std::uint32_t off) {
  if (!obj || !IsReadable(reinterpret_cast<std::uintptr_t>(obj) + off, 1)) {
    return 0;
  }
  return *static_cast<std::uint8_t*>(static_cast<std::uint8_t*>(obj) + off);
}

TraceSnap ReadSnap(void* self) {
  TraceSnap s;
  s.d0 = ReadI32(self, 0xD0);
  s.b8 = ReadI32(self, 0xB8);
  s.bc = ReadI32(self, 0xBC);
  s.c0 = ReadI32(self, 0xC0);
  s.c4 = ReadI32(self, 0xC4);
  s.dc = ReadI32(self, 0xDC);
  s.ce = ReadU16(self, 0xCE);
  s.d5 = ReadU8(self, 0xD5);
  return s;
}

void LogSnap(const char* tag, const TraceSnap& s) {
  char buf[160];
  snprintf(buf, sizeof(buf),
           "boss_bar: %s +0xD0=%d +0xB8=%d +0xBC=%d +0xC0=%d +0xC4=%d +0xDC=%d +0xCE=%u +0xD5=%u",
           tag, static_cast<int>(s.d0), static_cast<int>(s.b8), static_cast<int>(s.bc),
           static_cast<int>(s.c0), static_cast<int>(s.c4), static_cast<int>(s.dc),
           static_cast<unsigned>(s.ce), static_cast<unsigned>(s.d5));
  LogWrite(buf);
}

void __cdecl HookedTick(void* self) {
  if (self) {
    g_boss_bar.store(self, std::memory_order_release);
  }
  if (g_real_tick) {
    g_real_tick(self);
  }
}

bool PatchVtSlot(std::uintptr_t slot_rva, void* expected, void* detour, void*** out_slot,
                 FnThis* out_orig) {
  const auto base = GameBase();
  auto** slot = reinterpret_cast<void**>(base + slot_rva);
  if (!IsReadable(reinterpret_cast<std::uintptr_t>(slot), sizeof(void*))) {
    return false;
  }
  if (*slot != expected) {
    char buf[96];
    snprintf(buf, sizeof(buf), "boss_bar: vt slot mismatch @+0x%llX",
             static_cast<unsigned long long>(slot_rva));
    LogWrite(buf);
    return false;
  }
  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
    return false;
  }
  *out_orig = reinterpret_cast<FnThis>(*slot);
  *slot = detour;
  VirtualProtect(slot, sizeof(void*), old, &old);
  *out_slot = slot;
  return true;
}

void RestoreVtSlot(void** slot, FnThis orig) {
  if (!slot || !orig) {
    return;
  }
  DWORD old = 0;
  if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
    *slot = reinterpret_cast<void*>(orig);
    VirtualProtect(slot, sizeof(void*), old, &old);
  }
}

void* FindObjectByVtable(std::uintptr_t vtable) {
  MEMORY_BASIC_INFORMATION mbi{};
  std::uint8_t* addr = nullptr;
  std::size_t scanned = 0;
  constexpr std::size_t kMaxScan = 64ull * 1024ull * 1024ull;
  while (scanned < kMaxScan && VirtualQuery(addr, &mbi, sizeof(mbi))) {
    const std::size_t region = mbi.RegionSize;
    if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) &&
        !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && region <= 16ull * 1024ull * 1024ull) {
      auto* p = static_cast<std::uint8_t*>(mbi.BaseAddress);
      for (std::size_t i = 0; i + 8 <= region; i += 8) {
        std::uintptr_t val = 0;
        std::memcpy(&val, p + i, 8);
        if (val == vtable) {
          return p + i;
        }
      }
      scanned += region;
    }
    auto* next = static_cast<std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    if (next <= addr) {
      break;
    }
    addr = next;
  }
  return nullptr;
}

}  // namespace

void BossBarHooksConfigure(bool trace) { g_trace.store(trace, std::memory_order_relaxed); }

bool BossBarHooksInit() {
  if (g_ready.load(std::memory_order_relaxed)) {
    return true;
  }
  if (!GameModule() && !DetectFlavor()) {
    return false;
  }
  if (!IsScholar()) {
    LogWrite("boss_bar: FeSceneBossHpGuage hook is SotFS-only for now");
    return false;
  }
  const auto base = GameBase();
  if (!base) {
    return false;
  }
  if (!PatchVtSlot(kVtSlot4Rva, reinterpret_cast<void*>(base + kTickRva),
                   reinterpret_cast<void*>(&HookedTick), &g_slot_tick, &g_real_tick)) {
    LogWrite("boss_bar: failed to hook FeSceneBossHpGuage vt[4]");
    return false;
  }
  g_ready.store(true, std::memory_order_relaxed);
  if (g_trace.load(std::memory_order_relaxed)) {
    LogWrite("boss_bar: TRACE ON — FeSceneBossHpGuage fields");
  } else {
    LogWrite("boss_bar: FeSceneBossHpGuage hooked (observe)");
  }
  return true;
}

void BossBarHooksShutdown() {
  RestoreVtSlot(g_slot_tick, g_real_tick);
  g_slot_tick = nullptr;
  g_real_tick = nullptr;
  g_boss_bar.store(nullptr, std::memory_order_relaxed);
  g_ready.store(false, std::memory_order_relaxed);
  g_last_trace = {};
}

void BossBarHooksPoll() {
  if (!g_ready.load(std::memory_order_relaxed)) {
    return;
  }
  void* bar = g_boss_bar.load(std::memory_order_acquire);
  const auto now = GetTickCount64();
  if (!bar && (now - g_last_scan_ms) > 3000) {
    g_last_scan_ms = now;
    const auto base = GameBase();
    if (base) {
      bar = FindObjectByVtable(base + kVtRva);
      if (bar) {
        g_boss_bar.store(bar, std::memory_order_release);
        if (g_trace.load(std::memory_order_relaxed)) {
          char buf[80];
          snprintf(buf, sizeof(buf), "boss_bar: found FeSceneBossHpGuage @ %p", bar);
          LogWrite(buf);
        }
      }
    }
  }
  bar = g_boss_bar.load(std::memory_order_acquire);
  if (!bar || !g_trace.load(std::memory_order_relaxed)) {
    return;
  }
  const TraceSnap cur = ReadSnap(bar);
  if (cur == g_last_trace) {
    return;
  }
  g_last_trace = cur;
  LogSnap("fields", cur);
}

}  // namespace sitcom
