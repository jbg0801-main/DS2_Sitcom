#include "item_hooks.h"

#include "ds2_flavor.h"
#include "log.h"
#include "mem.h"

#include <windows.h>
#include <psapi.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sitcom {
namespace {

// DS2S-META ITEMID: Estus flask (filled/empty) and Sublime Bone Dust (rest upgrades).
constexpr int kEstus = 0x0395E478;
constexpr int kEstusEmpty = 0x0395E860;
constexpr int kBoneDust = 0x039B8DB0;

std::atomic<int> g_acquired{0};
std::atomic<int> g_last_item_id{0};
std::atomic<int> g_last_category{0};
std::atomic<int> g_suppressed{0};

struct InlineHook {
  void* target = nullptr;
  uint8_t* trampoline = nullptr;
  uint8_t stolen[16]{};
  SIZE_T patch_size = 0;
  bool active = false;
};

InlineHook g_hook;

bool IsRestockId(int id) {
  return id == kEstus || id == kEstusEmpty || id == kBoneDust;
}

bool LooksLikeItemId(int id) {
  const auto u = static_cast<unsigned>(id);
  return u >= 0x000F0000u && u <= 0x04FFFFFFu;
}

extern "C" {
void* g_item_get_continue = nullptr;

void ItemGetNotify(int a, int b, int c, int d) {
  int id = 0;
  if (LooksLikeItemId(a)) {
    id = a;
  } else if (LooksLikeItemId(b)) {
    id = b;
  } else if (LooksLikeItemId(c)) {
    id = c;
  } else if (LooksLikeItemId(d)) {
    id = d;
  }
  g_last_item_id.store(id, std::memory_order_relaxed);
  g_last_category.store(0, std::memory_order_relaxed);
  if (IsRestockId(a) || IsRestockId(b) || IsRestockId(c) || IsRestockId(d)) {
    g_suppressed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (id != 0) {
    g_acquired.fetch_add(1, std::memory_order_relaxed);
  }
}

#if defined(__GNUC__) && defined(_WIN64)
__attribute__((naked)) void HookedItemGetEntry() {
  __asm__ __volatile__(
      "push %%rax\n\t"
      "push %%rcx\n\t"
      "push %%rdx\n\t"
      "push %%r8\n\t"
      "push %%r9\n\t"
      "push %%r10\n\t"
      "push %%r11\n\t"
      "sub $0x28, %%rsp\n\t"
      "call ItemGetNotify\n\t"
      "add $0x28, %%rsp\n\t"
      "pop %%r11\n\t"
      "pop %%r10\n\t"
      "pop %%r9\n\t"
      "pop %%r8\n\t"
      "pop %%rdx\n\t"
      "pop %%rcx\n\t"
      "pop %%rax\n\t"
      "jmp *g_item_get_continue(%%rip)\n\t"
      :
      :
      :);
}
#elif defined(__GNUC__)
__attribute__((naked)) void HookedItemGetEntry() {
  __asm__ __volatile__(
      "pushal\n\t"
      "movl 48(%%esp), %%ebx\n\t"
      "movl 44(%%esp), %%edx\n\t"
      "movl 40(%%esp), %%ecx\n\t"
      "movl 36(%%esp), %%eax\n\t"
      "pushl %%ebx\n\t"
      "pushl %%edx\n\t"
      "pushl %%ecx\n\t"
      "pushl %%eax\n\t"
      "call _ItemGetNotify\n\t"
      "addl $16, %%esp\n\t"
      "popal\n\t"
      "jmp *_g_item_get_continue\n\t"
      :
      :
      :);
}
#else
#error "ItemGive naked hook requires GCC/MinGW"
#endif
}  // extern "C"

#if defined(_WIN64)
constexpr SIZE_T kPatch = 14;
#else
constexpr SIZE_T kPatch = 6;
#endif

bool InstallHook(void* target) {
  if (!target || !IsExecutable(target)) {
    return false;
  }
  g_hook.trampoline = static_cast<uint8_t*>(
      VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (!g_hook.trampoline) {
    return false;
  }
  g_hook.target = target;
  g_hook.patch_size = kPatch;
  std::memcpy(g_hook.stolen, target, kPatch);
  std::memcpy(g_hook.trampoline, g_hook.stolen, kPatch);

#if defined(_WIN64)
  g_hook.trampoline[kPatch] = 0x48;
  g_hook.trampoline[kPatch + 1] = 0xB8;
  const uint64_t ret = reinterpret_cast<uint64_t>(static_cast<uint8_t*>(target) + kPatch);
  std::memcpy(g_hook.trampoline + kPatch + 2, &ret, 8);
  g_hook.trampoline[kPatch + 10] = 0xFF;
  g_hook.trampoline[kPatch + 11] = 0xE0;
#else
  g_hook.trampoline[kPatch] = 0xE9;
  const uint32_t rel_back = static_cast<uint32_t>(
      reinterpret_cast<uint8_t*>(target) + kPatch - (g_hook.trampoline + kPatch + 5));
  std::memcpy(g_hook.trampoline + kPatch + 1, &rel_back, 4);
#endif
  g_item_get_continue = g_hook.trampoline;

  DWORD old = 0;
  if (!VirtualProtect(target, kPatch, PAGE_EXECUTE_READWRITE, &old)) {
    VirtualFree(g_hook.trampoline, 0, MEM_RELEASE);
    g_hook.trampoline = nullptr;
    g_item_get_continue = nullptr;
    return false;
  }
  auto* p = static_cast<uint8_t*>(target);
#if defined(_WIN64)
  p[0] = 0x48;
  p[1] = 0xB8;
  const uint64_t d = reinterpret_cast<uint64_t>(&HookedItemGetEntry);
  std::memcpy(p + 2, &d, 8);
  p[10] = 0xFF;
  p[11] = 0xE0;
  p[12] = 0x90;
  p[13] = 0x90;
#else
  p[0] = 0xE9;
  const uint32_t rel = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(&HookedItemGetEntry) -
                                             (p + 5));
  std::memcpy(p + 1, &rel, 4);
  p[5] = 0x90;
#endif
  VirtualProtect(target, kPatch, old, &old);
  FlushInstructionCache(GetCurrentProcess(), target, kPatch);
  g_hook.active = true;
  return true;
}

void RemoveHook() {
  if (!g_hook.active || !g_hook.target) {
    return;
  }
  DWORD old = 0;
  if (VirtualProtect(g_hook.target, g_hook.patch_size, PAGE_EXECUTE_READWRITE, &old)) {
    std::memcpy(g_hook.target, g_hook.stolen, g_hook.patch_size);
    VirtualProtect(g_hook.target, g_hook.patch_size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_hook.target, g_hook.patch_size);
  }
  if (g_hook.trampoline) {
    VirtualFree(g_hook.trampoline, 0, MEM_RELEASE);
    g_hook.trampoline = nullptr;
  }
  g_item_get_continue = nullptr;
  g_hook.active = false;
}

std::uintptr_t ScanItemGive() {
  const auto base = GameBase();
  const auto size = GameSize();
  if (!base) {
    return 0;
  }
  if (IsScholar()) {
    const std::uint8_t pat[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48,
                                0x83, 0xEC, 0x30, 0x45, 0x8B, 0xF1, 0x41};
    const char mask[] = "xxxxxxxxxxxxxxxxx";
    return PatternScan(base, size, pat, mask);
  }
  const std::uint8_t pat[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x8B, 0x5D, 0x0C,
                              0x56, 0x8B, 0x75, 0x08, 0x57, 0x53, 0x56, 0x8B, 0xF9};
  const char mask[] = "xxxxxxxxxxxxxxxxxxx";
  return PatternScan(base, size, pat, mask);
}

}  // namespace

bool ItemHooksInit() {
  if (g_hook.active) {
    return true;
  }
  if (!GameModule() && !DetectFlavor()) {
    LogWrite("item_hooks: game module not ready");
    return false;
  }
  const auto hit = ScanItemGive();
  if (!hit) {
    LogWrite("item_hooks: ItemGive AOB not found (ooh via anim only)");
    return false;
  }
  if (!InstallHook(reinterpret_cast<void*>(hit))) {
    LogWrite("item_hooks: failed to hook ItemGive");
    return false;
  }
  char buf[96];
  snprintf(buf, sizeof(buf), "item_hooks: ItemGive hooked @ 0x%llX",
           static_cast<unsigned long long>(hit));
  LogWrite(buf);
  return true;
}

void ItemHooksShutdown() {
  RemoveHook();
  g_acquired.store(0, std::memory_order_relaxed);
  g_suppressed.store(0, std::memory_order_relaxed);
}

int ItemHooksConsumeAcquired() { return g_acquired.exchange(0, std::memory_order_relaxed); }
int ItemHooksConsumeSuppressed() { return g_suppressed.exchange(0, std::memory_order_relaxed); }
int ItemHooksLastItemId() { return g_last_item_id.load(std::memory_order_relaxed); }
int ItemHooksLastCategory() { return g_last_category.load(std::memory_order_relaxed); }

}  // namespace sitcom
