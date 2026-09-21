#include "estus_hooks.h"

#include "ds2_flavor.h"
#include "log.h"
#include "mem.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace sitcom {
namespace {

// Bob SotFS Estus charge function (prologue ~0x1AE080):
//   success: movzx ecx,[rax+24h]; lea edx,[rcx-1]; … jmp setter   @ 0x1AE0BF
//   empty:   xor ecx,ecx; lea edx,[rcx-1]; … jmp setter           @ 0x1AE0D3
// Empty drinks take the fail branch (unique shake/head-shake anim) and never
// reach the decrement site — hook that branch only so filled chugs stay virgin.

std::atomic<int> g_empty_hits{0};
std::atomic<bool> g_ready{false};

#if defined(_WIN64)
struct InlineHook {
  void* target = nullptr;
  uint8_t* trampoline = nullptr;
  uint8_t stolen[16]{};
  SIZE_T patch_size = 0;
  bool active = false;
};

InlineHook g_hook;

constexpr std::uintptr_t kEmptyBranchRva = 0x1AE0D3;
// xor ecx,ecx; lea edx,[rcx-1]; mov rcx,rbx; add rsp,20h; pop rbx; jmp …
constexpr std::uint8_t kEmptyBranchBytes[] = {0x33, 0xC9, 0x8D, 0x51, 0xFF, 0x48, 0x8B,
                                              0xCB, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xE9};
constexpr SIZE_T kPatch = 14;
constexpr std::uintptr_t kSetterRva = 0x1AEA00;

extern "C" void EstusEmptyNotify() {
  g_empty_hits.fetch_add(1, std::memory_order_release);
}

extern "C" void HookedEstusEmptyEntry();
extern "C" {
void* g_estus_continue = nullptr;
}

// rbx = flask owner (untouched). Notify then run the real empty-branch body.
__attribute__((naked)) void HookedEstusEmptyEntry() {
  __asm__ __volatile__(
      ".intel_syntax noprefix\n\t"
      "pushfq\n\t"
      "push rax\n\t"
      "push rcx\n\t"
      "push rdx\n\t"
      "push r8\n\t"
      "push r9\n\t"
      "push r10\n\t"
      "push r11\n\t"
      "sub rsp, 0x28\n\t"
      "call EstusEmptyNotify\n\t"
      "add rsp, 0x28\n\t"
      "pop r11\n\t"
      "pop r10\n\t"
      "pop r9\n\t"
      "pop r8\n\t"
      "pop rdx\n\t"
      "pop rcx\n\t"
      "pop rax\n\t"
      "popfq\n\t"
      "jmp qword ptr [rip+g_estus_continue]\n\t"
      ".att_syntax prefix\n\t");
}

bool InstallHook(void* target, std::uintptr_t game_base) {
  if (!target || !IsExecutable(target)) {
    return false;
  }
  if (std::memcmp(target, kEmptyBranchBytes, sizeof(kEmptyBranchBytes)) != 0) {
    LogWrite("estus: empty-branch bytes mismatch");
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

  // Replay xor/lea/mov/add/pop (13 bytes), then abs-jmp to shared setter.
  // (Patch steals the first byte of the original rel32 jmp — do not copy it.)
  constexpr SIZE_T kBody = 13;
  std::memcpy(g_hook.trampoline, g_hook.stolen, kBody);
  g_hook.trampoline[kBody] = 0xFF;
  g_hook.trampoline[kBody + 1] = 0x25;
  g_hook.trampoline[kBody + 2] = 0x00;
  g_hook.trampoline[kBody + 3] = 0x00;
  g_hook.trampoline[kBody + 4] = 0x00;
  g_hook.trampoline[kBody + 5] = 0x00;
  const uint64_t setter = game_base + kSetterRva;
  std::memcpy(g_hook.trampoline + kBody + 6, &setter, 8);
  g_estus_continue = g_hook.trampoline;

  DWORD old = 0;
  if (!VirtualProtect(target, kPatch, PAGE_EXECUTE_READWRITE, &old)) {
    VirtualFree(g_hook.trampoline, 0, MEM_RELEASE);
    g_hook.trampoline = nullptr;
    g_estus_continue = nullptr;
    return false;
  }
  // jmp qword ptr [rip+0]; dq HookedEstusEmptyEntry
  auto* p = static_cast<uint8_t*>(target);
  p[0] = 0xFF;
  p[1] = 0x25;
  p[2] = 0x00;
  p[3] = 0x00;
  p[4] = 0x00;
  p[5] = 0x00;
  const uint64_t d = reinterpret_cast<uint64_t>(&HookedEstusEmptyEntry);
  std::memcpy(p + 6, &d, 8);
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
  g_estus_continue = nullptr;
  g_hook.active = false;
}
#endif

}  // namespace

bool EstusHooksInit() {
  if (g_ready.load(std::memory_order_relaxed)) {
    return true;
  }
  if (!GameModule() && !DetectFlavor()) {
    return false;
  }
  if (!IsScholar()) {
    LogWrite("estus: empty-flask hook is SotFS-only for now");
    return false;
  }
#if defined(_WIN64)
  const auto base = GameBase();
  if (!base ||
      !InstallHook(reinterpret_cast<void*>(base + kEmptyBranchRva), base)) {
    LogWrite("estus: failed to hook empty-flask branch");
    return false;
  }
  g_ready.store(true, std::memory_order_relaxed);
  LogWrite("estus: empty-flask branch hooked (0x1AE0D3 fail path)");
  return true;
#else
  return false;
#endif
}

void EstusHooksShutdown() {
#if defined(_WIN64)
  RemoveHook();
#endif
  g_ready.store(false, std::memory_order_relaxed);
  g_empty_hits.store(0, std::memory_order_relaxed);
}

int EstusHooksConsumeEmpty() { return g_empty_hits.exchange(0, std::memory_order_acq_rel); }

}  // namespace sitcom
