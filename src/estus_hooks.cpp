#include "estus_hooks.h"

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

// Bob SotFS Estus charge function (prologue ~0x1AE080):
//   success: movzx ecx,[rax+24h]; lea edx,[rcx-1]; … jmp setter   @ 0x1AE0BF
//   empty:   xor ecx,ecx; lea edx,[rcx-1]; … jmp setter           @ 0x1AE0D3
// Empty drinks take the fail branch (unique shake/head-shake anim) and never
// reach the decrement site — hook that branch only so filled chugs stay virgin.

std::atomic<int> g_empty_hits{0};
std::atomic<int> g_motion_seq{0};
std::atomic<int> g_motion_vals[8]{};
std::atomic<bool> g_ready{false};

extern "C" void EstusEmptyNotify() {
  g_empty_hits.fetch_add(1, std::memory_order_release);
}

// 0x75C0BF, after the motion fields are written onto the character.
// sel is the byte that chooses 0x35A versus the item's own id at +0x6F0.
extern "C" __attribute__((used)) void EstusMotionNote(int store, int sel, int e6e0, int e6e8,
                                                     int e6f0, int e6f4, int e6f8, int e6fc) {
  const int vals[8] = {store, sel, e6e0, e6e8, e6f0, e6f4, e6f8, e6fc};
  for (int i = 0; i < 8; ++i) {
    g_motion_vals[i].store(vals[i], std::memory_order_relaxed);
  }
  // Full chugs write sel=0 and motion id 855. The empty shake writes sel=1
  // and forces id 858 (0x35A) at character+0x6F0.
  if (sel == 1 && e6f0 == 858) {
    g_empty_hits.fetch_add(1, std::memory_order_release);
  }
  g_motion_seq.fetch_add(1, std::memory_order_release);
}

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
#else
// After 0x75BE30 has written the motion ids. 6 bytes, replayed exactly:
//   89 83 00 07 00 00   mov dword ptr [ebx+0x700], eax
struct InlineHook {
  void* target = nullptr;
  uint8_t stolen[16]{};
  SIZE_T patch_size = 0;
  bool active = false;
};

InlineHook g_hook;
extern "C" void* g_estus_continue = nullptr;
extern "C" __attribute__((naked)) void HookedEstusEmptyUse();

constexpr std::uintptr_t kMotionRva = 0x35C0BF;
constexpr std::uintptr_t kMotionNextRva = 0x35C0C5;
constexpr std::uint8_t kMotionBytes[] = {0x89, 0x83, 0x00, 0x07, 0x00, 0x00};

extern "C" __attribute__((naked)) void HookedEstusEmptyUse() {
  __asm__ __volatile__(
      ".intel_syntax noprefix\n\t"
      "pushfd\n\t"
      "pushad\n\t"
      "mov ebx, dword ptr [esp + 0x10]\n\t"
      "mov eax, dword ptr [esp + 0x1c]\n\t"
      "mov ebp, dword ptr [esp + 8]\n\t"
      "movzx ecx, byte ptr [ebp - 0x13]\n\t"
      "mov edx, dword ptr [ebx + 0x6e0]\n\t"
      "mov esi, dword ptr [ebx + 0x6e8]\n\t"
      "mov edi, dword ptr [ebx + 0x6f0]\n\t"
      "push dword ptr [ebx + 0x6fc]\n\t"
      "push dword ptr [ebx + 0x6f8]\n\t"
      "push dword ptr [ebx + 0x6f4]\n\t"
      "push edi\n\t"
      "push esi\n\t"
      "push edx\n\t"
      "push ecx\n\t"
      "push eax\n\t"
      "call _EstusMotionNote\n\t"
      "add esp, 32\n\t"
      "popad\n\t"
      "popfd\n\t"
      "mov dword ptr [ebx + 0x700], eax\n\t"
      "jmp dword ptr [_g_estus_continue]\n\t"
      ".att_syntax prefix\n\t");
}

bool InstallHook(void* target, std::uintptr_t game_base) {
  if (!target || !IsExecutable(target)) {
    return false;
  }
  if (std::memcmp(target, kMotionBytes, sizeof(kMotionBytes)) != 0) {
    LogWrite("estus: motion-args bytes mismatch");
    return false;
  }
  g_hook.target = target;
  g_hook.patch_size = sizeof(kMotionBytes);
  std::memcpy(g_hook.stolen, target, g_hook.patch_size);
  g_estus_continue = reinterpret_cast<void*>(game_base + kMotionNextRva);

  DWORD old = 0;
  if (!VirtualProtect(target, g_hook.patch_size, PAGE_EXECUTE_READWRITE, &old)) {
    g_estus_continue = nullptr;
    return false;
  }
  auto* p = static_cast<uint8_t*>(target);
  const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&HookedEstusEmptyUse) -
                                              (reinterpret_cast<std::uintptr_t>(target) + 5));
  p[0] = 0xE9;
  std::memcpy(p + 1, &rel, 4);
  std::memset(p + 5, 0x90, g_hook.patch_size - 5);
  VirtualProtect(target, g_hook.patch_size, old, &old);
  FlushInstructionCache(GetCurrentProcess(), target, g_hook.patch_size);
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
#if defined(_WIN64)
  if (!IsScholar()) {
    LogWrite("estus: empty-flask hook is SotFS-only for now");
    g_ready.store(true, std::memory_order_relaxed);
    return true;
  }
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
  const auto base = GameBase();
  if (!base ||
      !InstallHook(reinterpret_cast<void*>(base + kMotionRva), base)) {
    LogWrite("estus: failed to hook empty-flask use");
    return false;
  }
  g_ready.store(true, std::memory_order_relaxed);
  LogWrite("estus: empty shake hooked (vanilla 0x35C0BF, sel=1)");
  return true;
#endif
}

void EstusHooksShutdown() {
  RemoveHook();
  g_ready.store(false, std::memory_order_relaxed);
  g_empty_hits.store(0, std::memory_order_relaxed);
}

int EstusHooksConsumeEmpty() {
  static int seen_seq = 0;
  const int seq = g_motion_seq.load(std::memory_order_acquire);
  if (seq != seen_seq) {
    seen_seq = seq;
    char buf[192];
    snprintf(buf, sizeof(buf),
             "estus: body store=%d sel=%d e6e0=%d e6e8=%d e6f0=%d e6f4=%d e6f8=%d e6fc=%d",
             g_motion_vals[0].load(std::memory_order_relaxed),
             g_motion_vals[1].load(std::memory_order_relaxed),
             g_motion_vals[2].load(std::memory_order_relaxed),
             g_motion_vals[3].load(std::memory_order_relaxed),
             g_motion_vals[4].load(std::memory_order_relaxed),
             g_motion_vals[5].load(std::memory_order_relaxed),
             g_motion_vals[6].load(std::memory_order_relaxed),
             g_motion_vals[7].load(std::memory_order_relaxed));
    LogWrite(buf);
  }
  const int hits = g_empty_hits.exchange(0, std::memory_order_acq_rel);
  if (hits > 0) {
    LogWrite("estus: empty shake");
  }
  return hits;
}

}  // namespace sitcom
