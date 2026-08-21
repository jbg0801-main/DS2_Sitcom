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

// FeSubStateTitleInformation +0x10 state machine drives PlaceName.
// vt[1] sets 1 on first card; later cards often change +0x10 without vt[1].
// Capture `this` on activate, then poll idle → show-start transitions.

std::atomic<int> g_title_hits{0};
std::atomic<bool> g_ready{false};
std::atomic<void*> g_title_info{nullptr};
std::atomic<std::int32_t> g_last_state{0};

using FnActivate = void(__cdecl*)(void* self);

void** g_vt_activate_slot = nullptr;
FnActivate g_real_activate = nullptr;

bool IsIdleState(std::int32_t s) { return s == 0 || s == -1 || s == 4; }
bool IsShowStartState(std::int32_t s) { return s == 1 || s == 2 || s == 5; }

std::int32_t ReadState(void* self) {
  if (!self || !IsReadable(reinterpret_cast<std::uintptr_t>(self) + 0x10, 4)) {
    return 0;
  }
  return *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(self) + 0x10);
}

void NoteShow(const char* why, std::int32_t from, std::int32_t to) {
  g_title_hits.fetch_add(1, std::memory_order_release);
  char buf[120];
  snprintf(buf, sizeof(buf), "area_title: PlaceName show (%s) state %d→%d", why,
           static_cast<int>(from), static_cast<int>(to));
  LogWrite(buf);
}

void __cdecl HookedActivate(void* self) {
  const std::int32_t before = ReadState(self);
  if (g_real_activate) {
    g_real_activate(self);
  } else if (self) {
    *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(self) + 0x10) = 1;
  }
  g_title_info.store(self, std::memory_order_release);
  const std::int32_t after = ReadState(self);
  g_last_state.store(after, std::memory_order_release);
  if (IsIdleState(before) && IsShowStartState(after)) {
    NoteShow("activate", before, after);
  }
}

constexpr std::uintptr_t kActivateRva = 0xFF570;
constexpr std::uintptr_t kVtableSlot1Rva = 0x10BDCD0;
constexpr std::uint8_t kActivateBytes[] = {0xC7, 0x41, 0x10, 0x01, 0x00, 0x00, 0x00, 0xC3};

bool HookActivateSlot() {
  const auto base = GameBase();
  if (!base) {
    return false;
  }
  const auto activate = reinterpret_cast<void*>(base + kActivateRva);
  auto** slot = reinterpret_cast<void**>(base + kVtableSlot1Rva);
  if (!IsReadable(base + kActivateRva, sizeof(kActivateBytes)) ||
      !IsReadable(reinterpret_cast<std::uintptr_t>(slot), sizeof(void*))) {
    return false;
  }
  if (std::memcmp(reinterpret_cast<const void*>(base + kActivateRva), kActivateBytes,
                  sizeof(kActivateBytes)) != 0) {
    LogWrite("area_title: activate bytes mismatch");
    return false;
  }
  if (*slot != activate) {
    LogWrite("area_title: vtable slot does not point at activate");
    return false;
  }

  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
    return false;
  }
  g_real_activate = reinterpret_cast<FnActivate>(*slot);
  *slot = reinterpret_cast<void*>(&HookedActivate);
  VirtualProtect(slot, sizeof(void*), old, &old);
  g_vt_activate_slot = slot;
  return true;
}

void UnhookActivateSlot() {
  if (!g_vt_activate_slot || !g_real_activate) {
    return;
  }
  DWORD old = 0;
  if (VirtualProtect(g_vt_activate_slot, sizeof(void*), PAGE_READWRITE, &old)) {
    *g_vt_activate_slot = reinterpret_cast<void*>(g_real_activate);
    VirtualProtect(g_vt_activate_slot, sizeof(void*), old, &old);
  }
  g_vt_activate_slot = nullptr;
  g_real_activate = nullptr;
}

}  // namespace

bool AreaTitleHooksInit() {
  if (g_ready.load(std::memory_order_relaxed)) {
    return true;
  }
  if (!GameModule() && !DetectFlavor()) {
    LogWrite("area_title: game module not ready");
    return false;
  }
  if (!IsScholar()) {
    LogWrite("area_title: TitleInformation hook is SotFS-only for now");
    return false;
  }
  if (!HookActivateSlot()) {
    LogWrite("area_title: failed to hook TitleInformation activate");
    return false;
  }
  g_ready.store(true, std::memory_order_relaxed);
  LogWrite("area_title: TitleInformation activate hooked; polling +0x10 for re-shows");
  return true;
}

void AreaTitleHooksShutdown() {
  UnhookActivateSlot();
  g_title_info.store(nullptr, std::memory_order_relaxed);
  g_ready.store(false, std::memory_order_relaxed);
  g_title_hits.store(0, std::memory_order_relaxed);
}

void AreaTitleHooksPoll() {
  void* self = g_title_info.load(std::memory_order_acquire);
  if (!self) {
    return;
  }
  const std::int32_t st = ReadState(self);
  const std::int32_t prev = g_last_state.load(std::memory_order_acquire);
  if (st == prev) {
    return;
  }
  g_last_state.store(st, std::memory_order_release);
  char buf[96];
  snprintf(buf, sizeof(buf), "area_title: state %d→%d", static_cast<int>(prev),
           static_cast<int>(st));
  LogWrite(buf);
  if (IsIdleState(prev) && IsShowStartState(st)) {
    NoteShow("poll", prev, st);
  }
}

int AreaTitleHooksPending() { return g_title_hits.load(std::memory_order_acquire); }

int AreaTitleHooksConsume() { return g_title_hits.exchange(0, std::memory_order_acq_rel); }

}  // namespace sitcom
