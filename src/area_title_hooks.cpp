#include "area_title_hooks.h"

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

// Live Proton trace (Betwixt→Majula): displayed PlaceName text id.
//   Betwixt:  … → 100200
//   Majula:   100200 → 100400
// TitleInformation activate only fires on some first-load cards — not Majula walks.
// FeSceneMapName vt[4] is a hot tick (capture `this` only). vt[3] clears the
// displayed id to -1. SotFS stores it at +0x1C (pending +0x18). Vanilla 1.12
// stores it at +0x10 (pending +0xC). vt[4] is thiscall and returns `ret 4`.

std::atomic<int> g_title_hits{0};
std::atomic<bool> g_ready{false};
std::atomic<bool> g_trace{false};
std::atomic<void*> g_map_name{nullptr};
std::atomic<std::int32_t> g_last_place_id{0x7FFFFFFF};

#if defined(_WIN64)
using FnTick = void(__cdecl*)(void* self);
constexpr std::uintptr_t kMnTickRva = 0x684F0;
constexpr std::uintptr_t kMnVtSlot4Rva = 0x10FA800;
constexpr std::uintptr_t kMnVtRva = 0x10FA7E0;
constexpr std::uint32_t kPlaceOff = 0x1C;
constexpr std::uint32_t kPendingOff = 0x18;
#else
using FnTick = void(__attribute__((thiscall)) *)(void* self, void* arg);
constexpr std::uintptr_t kMnTickRva = 0x128140;
constexpr std::uintptr_t kMnVtRva = 0xEE9130;
constexpr std::uintptr_t kMnVtSlot4Rva = 0xEE9140;  // vt + 4 * sizeof(void*)
constexpr std::uint32_t kPlaceOff = 0x10;
constexpr std::uint32_t kPendingOff = 0xC;
#endif

void** g_slot_mn_tick = nullptr;
FnTick g_real_mn_tick = nullptr;

std::int32_t ReadI32(void* obj, std::uint32_t off) {
  if (!obj || !IsReadable(reinterpret_cast<std::uintptr_t>(obj) + off, 4)) {
    return 0;
  }
  return *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(obj) + off);
}

// PlaceName msg ids observed in the mid-100000s; ignore idle/sentinel values.
bool IsPlaceNameId(std::int32_t id) {
  return id >= 100000 && id < 10000000;
}

// Connector sub-maps update +0x1C but never show a title card.
// m10_30 Heide ↔ Wharf (spiral exit), m10_29 Majula ↔ Shaded Woods.
bool IsConnectorPlaceId(std::int32_t id) { return id == 103000 || id == 102900; }

void NotePlaceId(std::int32_t from, std::int32_t to) {
  g_title_hits.fetch_add(1, std::memory_order_release);
  char buf[96];
  snprintf(buf, sizeof(buf), "area_title: PlaceName id %d→%d (MapName+0x1C)",
           static_cast<int>(from), static_cast<int>(to));
  LogWrite(buf);
}

void CaptureMapName(void* self) {
  if (self) {
    g_map_name.store(self, std::memory_order_release);
  }
  if (g_trace.load(std::memory_order_relaxed) && self) {
    static std::atomic<int> spam{0};
    const auto id = ReadI32(self, kPlaceOff);
    const auto prev = g_last_place_id.load(std::memory_order_relaxed);
    if (id != prev || spam.fetch_add(1, std::memory_order_relaxed) < 4) {
      char buf[140];
      snprintf(buf, sizeof(buf),
               "area_trace: MapName vt[4] this=%p pending(+0x%X)=%d displayed(+0x%X)=%d", self,
               kPendingOff, static_cast<int>(ReadI32(self, kPendingOff)), kPlaceOff,
               static_cast<int>(id));
      LogWrite(buf);
    }
  }
}

#if defined(_WIN64)
void __cdecl HookedMnTick(void* self) {
  CaptureMapName(self);
  if (g_real_mn_tick) {
    g_real_mn_tick(self);
  }
}
#else
void __attribute__((thiscall)) HookedMnTick(void* self, void* arg) {
  CaptureMapName(self);
  if (g_real_mn_tick) {
    g_real_mn_tick(self, arg);
  }
}
#endif

bool PatchVtSlot(std::uintptr_t slot_rva, void* expected, void* detour, void*** out_slot,
                 FnTick* out_orig) {
  const auto base = GameBase();
  auto** slot = reinterpret_cast<void**>(base + slot_rva);
  if (!IsReadable(reinterpret_cast<std::uintptr_t>(slot), sizeof(void*))) {
    return false;
  }
  if (*slot != expected) {
    char buf[96];
    snprintf(buf, sizeof(buf), "area_title: vt slot mismatch @+0x%llX",
             static_cast<unsigned long long>(slot_rva));
    LogWrite(buf);
    return false;
  }
  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
    return false;
  }
  *out_orig = reinterpret_cast<FnTick>(*slot);
  *slot = detour;
  VirtualProtect(slot, sizeof(void*), old, &old);
  *out_slot = slot;
  return true;
}

void RestoreVtSlot(void** slot, FnTick orig) {
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
      constexpr std::size_t kPtr = sizeof(void*);
      for (std::size_t i = 0; i + kPtr <= region; i += kPtr) {
        std::uintptr_t val = 0;
        std::memcpy(&val, p + i, kPtr);
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

std::uint64_t g_last_scan_ms = 0;

}  // namespace

void AreaTitleHooksConfigure(bool trace) { g_trace.store(trace, std::memory_order_relaxed); }

bool AreaTitleHooksInit() {
  if (g_ready.load(std::memory_order_relaxed)) {
    return true;
  }
  if (!GameModule() && !DetectFlavor()) {
    LogWrite("area_title: game module not ready");
    return false;
  }
  const auto base = GameBase();
  if (!base) {
    return false;
  }
  if (!PatchVtSlot(kMnVtSlot4Rva, reinterpret_cast<void*>(base + kMnTickRva),
                   reinterpret_cast<void*>(&HookedMnTick), &g_slot_mn_tick, &g_real_mn_tick)) {
    LogWrite("area_title: failed to hook FeSceneMapName vt[4]");
    return false;
  }
  g_ready.store(true, std::memory_order_relaxed);
  if (g_trace.load(std::memory_order_relaxed)) {
    LogWrite("area_title: TRACE ON — MapName displayed PlaceName ids");
  } else {
    char buf[96];
    snprintf(buf, sizeof(buf), "area_title: FeSceneMapName hooked (PlaceName via +0x%X)", kPlaceOff);
    LogWrite(buf);
  }
  return true;
}

void AreaTitleHooksShutdown() {
  RestoreVtSlot(g_slot_mn_tick, g_real_mn_tick);
  g_slot_mn_tick = nullptr;
  g_real_mn_tick = nullptr;
  g_map_name.store(nullptr, std::memory_order_relaxed);
  g_ready.store(false, std::memory_order_relaxed);
  g_title_hits.store(0, std::memory_order_relaxed);
  g_last_place_id.store(0x7FFFFFFF, std::memory_order_relaxed);
}

void AreaTitleHooksPoll() {
  void* mn = g_map_name.load(std::memory_order_acquire);
  const auto now = GetTickCount64();
  if (!mn && (now - g_last_scan_ms) > 3000) {
    g_last_scan_ms = now;
    const auto base = GameBase();
    if (base) {
      mn = FindObjectByVtable(base + kMnVtRva);
      if (mn) {
        g_map_name.store(mn, std::memory_order_release);
        if (g_trace.load(std::memory_order_relaxed)) {
          char buf[80];
          snprintf(buf, sizeof(buf), "area_trace: found FeSceneMapName @ %p", mn);
          LogWrite(buf);
        }
      }
    }
  }
  mn = g_map_name.load(std::memory_order_acquire);
  if (!mn) {
    return;
  }

  const std::int32_t id = ReadI32(mn, kPlaceOff);
  const std::int32_t prev = g_last_place_id.load(std::memory_order_acquire);
  if (id == prev) {
    return;
  }
  g_last_place_id.store(id, std::memory_order_release);

  if (g_trace.load(std::memory_order_relaxed)) {
    char buf[120];
    snprintf(buf, sizeof(buf), "area_trace: MapName+0x%X %d→%d (pending +0x%X=%d)", kPlaceOff,
             static_cast<int>(prev), static_cast<int>(id), kPendingOff,
             static_cast<int>(ReadI32(mn, kPendingOff)));
    LogWrite(buf);
  }

  // Clear-to-idle is not a card. Anything else outside the known band is logged
  // so a live session can pin a new id without another build.
  if (id < 0) {
    return;
  }
  if (!IsPlaceNameId(id)) {
    char buf[96];
    snprintf(buf, sizeof(buf), "area_title: unmapped PlaceName id %d→%d", static_cast<int>(prev),
             static_cast<int>(id));
    LogWrite(buf);
    return;
  }

  // Rising/change to a real PlaceName id (not a connector sub-map).
  if (id != prev) {
    if (IsConnectorPlaceId(id)) {
      char buf[96];
      snprintf(buf, sizeof(buf), "area_title: skip connector PlaceName id %d→%d",
               static_cast<int>(prev), static_cast<int>(id));
      LogWrite(buf);
      return;
    }
    NotePlaceId(prev, id);
  }
}

int AreaTitleHooksPending() { return g_title_hits.load(std::memory_order_acquire); }

int AreaTitleHooksConsume() { return g_title_hits.exchange(0, std::memory_order_acq_rel); }

}  // namespace sitcom
