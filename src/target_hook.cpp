#include "target_hook.h"

#include "log.h"

#include <windows.h>
#include <psapi.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace sitcom {
namespace {

std::atomic<std::uintptr_t> g_locked_target{0};
std::uint8_t* g_patch_addr = nullptr;
std::uint8_t g_orig_bytes[16]{};
std::size_t g_patch_size = 0;
void* g_trampoline = nullptr;
bool g_installed = false;

// 48 8B 03 | 48 8B CB | E9 xx xx xx xx
constexpr std::uint8_t kLead[] = {0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xE9};

extern "C" void SitcomCaptureTarget(std::uintptr_t target) {
  g_locked_target.store(target, std::memory_order_relaxed);
}

std::uintptr_t PatternScan(std::uintptr_t base, std::size_t size) {
  if (size < 21) {
    return 0;
  }
  for (std::size_t i = 0; i + 21 <= size; ++i) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(base + i);
    if (std::memcmp(p, kLead, 6) != 0 || p[6] != 0xE9) {
      continue;
    }
    // Unique tail from Phokz CT: after jmp, two lea rsp,[rsp+8]
    if (p[11] == 0x48 && p[12] == 0x8D && p[13] == 0x64 && p[14] == 0x24 && p[15] == 0x08 &&
        p[16] == 0x48 && p[17] == 0x8D && p[18] == 0x64 && p[19] == 0x24 && p[20] == 0x08) {
      return base + i;
    }
  }
  return 0;
}

void* AllocNear(void* near_addr, std::size_t size) {
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  const auto gran = static_cast<std::uintptr_t>(si.dwAllocationGranularity);
  auto base = reinterpret_cast<std::uintptr_t>(near_addr) & ~(gran - 1);

  for (std::uintptr_t delta = gran; delta < 0x70000000ull; delta += gran) {
    for (int sign = -1; sign <= 1; sign += 2) {
      const std::uintptr_t candidate = base + static_cast<std::uintptr_t>(sign) * delta;
      void* p = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE,
                             PAGE_EXECUTE_READWRITE);
      if (p) {
        return p;
      }
    }
  }
  return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

void WriteRel32Jmp(void* at, void* to) {
  auto* p = static_cast<std::uint8_t*>(at);
  const auto rel =
      static_cast<std::int32_t>(reinterpret_cast<std::uint8_t*>(to) - (p + 5));
  p[0] = 0xE9;
  std::memcpy(p + 1, &rel, 4);
}

void WriteAbsJmp(void* at, void* to) {
  auto* p = static_cast<std::uint8_t*>(at);
  p[0] = 0xFF;
  p[1] = 0x25;
  *reinterpret_cast<std::int32_t*>(p + 2) = 0;
  *reinterpret_cast<std::uint64_t*>(p + 6) = reinterpret_cast<std::uint64_t>(to);
}

}  // namespace

bool InstallTargetHook() {
  if (g_installed) {
    return true;
  }

  HMODULE game = GetModuleHandleW(L"DarkSoulsRemastered.exe");
  if (!game) {
    LogWrite("target_hook: game module not found");
    return false;
  }

  MODULEINFO info{};
  if (!GetModuleInformation(GetCurrentProcess(), game, &info, sizeof(info))) {
    LogWrite("target_hook: GetModuleInformation failed");
    return false;
  }

  const auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
  const auto hit = PatternScan(base, info.SizeOfImage);
  if (!hit) {
    LogWrite("target_hook: pattern not found (boss lock tracking disabled)");
    return false;
  }

  g_patch_addr = reinterpret_cast<std::uint8_t*>(hit);
  // Original instruction group is 11 bytes; decode jmp destination.
  const auto rel = *reinterpret_cast<std::int32_t*>(g_patch_addr + 7);
  void* cont = g_patch_addr + 11 + rel;

  g_trampoline = AllocNear(g_patch_addr, 128);
  if (!g_trampoline) {
    LogWrite("target_hook: VirtualAlloc failed");
    return false;
  }

  auto* tramp = static_cast<std::uint8_t*>(g_trampoline);
  std::size_t o = 0;
  // Preserve rax/rcx around helper call; game expects rax=[rbx], rcx=rbx afterwards.
  tramp[o++] = 0x50;  // push rax
  tramp[o++] = 0x51;  // push rcx
  tramp[o++] = 0x48;
  tramp[o++] = 0x89;
  tramp[o++] = 0xD9;  // mov rcx, rbx
  tramp[o++] = 0x48;
  tramp[o++] = 0x83;
  tramp[o++] = 0xEC;
  tramp[o++] = 0x20;  // sub rsp, 0x20
  tramp[o++] = 0x48;
  tramp[o++] = 0xB8;  // mov rax, imm64
  *reinterpret_cast<std::uint64_t*>(tramp + o) =
      reinterpret_cast<std::uint64_t>(&SitcomCaptureTarget);
  o += 8;
  tramp[o++] = 0xFF;
  tramp[o++] = 0xD0;  // call rax
  tramp[o++] = 0x48;
  tramp[o++] = 0x83;
  tramp[o++] = 0xC4;
  tramp[o++] = 0x20;  // add rsp, 0x20
  tramp[o++] = 0x59;  // pop rcx
  tramp[o++] = 0x58;  // pop rax
  // Original first two instructions
  tramp[o++] = 0x48;
  tramp[o++] = 0x8B;
  tramp[o++] = 0x03;  // mov rax, [rbx]
  tramp[o++] = 0x48;
  tramp[o++] = 0x8B;
  tramp[o++] = 0xCB;  // mov rcx, rbx
  WriteAbsJmp(tramp + o, cont);

  // Prefer 5-byte relative patch if trampoline is in range; else 14-byte abs.
  const auto dist = reinterpret_cast<std::intptr_t>(tramp) -
                    reinterpret_cast<std::intptr_t>(g_patch_addr + 5);
  const bool near_ok = dist >= INT32_MIN && dist <= INT32_MAX;
  g_patch_size = near_ok ? 5 : 14;
  std::memcpy(g_orig_bytes, g_patch_addr, g_patch_size);

  DWORD old_prot = 0;
  if (!VirtualProtect(g_patch_addr, g_patch_size, PAGE_EXECUTE_READWRITE, &old_prot)) {
    LogWrite("target_hook: VirtualProtect failed");
    VirtualFree(g_trampoline, 0, MEM_RELEASE);
    g_trampoline = nullptr;
    return false;
  }

  if (near_ok) {
    WriteRel32Jmp(g_patch_addr, tramp);
    // NOP the remainder of the 11-byte original group so we don't leave a torn jmp.
    for (std::size_t i = 5; i < 11; ++i) {
      g_patch_addr[i] = 0x90;
    }
    g_patch_size = 11;  // restore full group on uninstall
    // We only saved 5 bytes earlier — expand saved original to full 11.
    // Re-read from... we already overwrote. Reconstruct:
    // 48 8B 03 48 8B CB E9 rel32 — need original rel from `cont`.
    const auto orig_rel =
        static_cast<std::int32_t>(reinterpret_cast<std::uint8_t*>(cont) - (g_patch_addr + 11));
    g_orig_bytes[0] = 0x48;
    g_orig_bytes[1] = 0x8B;
    g_orig_bytes[2] = 0x03;
    g_orig_bytes[3] = 0x48;
    g_orig_bytes[4] = 0x8B;
    g_orig_bytes[5] = 0xCB;
    g_orig_bytes[6] = 0xE9;
    std::memcpy(g_orig_bytes + 7, &orig_rel, 4);
  } else {
    WriteAbsJmp(g_patch_addr, tramp);
    // Pad remaining of 14 with nops if we need to cover only 11? Abs uses 14, covering past
    // into lea — avoid that. Fail near alloc instead.
    LogWrite("target_hook: trampoline too far; aborting install");
    std::memcpy(g_patch_addr, g_orig_bytes, 14);
    VirtualProtect(g_patch_addr, 14, old_prot, &old_prot);
    VirtualFree(g_trampoline, 0, MEM_RELEASE);
    g_trampoline = nullptr;
    g_patch_addr = nullptr;
    return false;
  }

  VirtualProtect(g_patch_addr, g_patch_size, old_prot, &old_prot);
  FlushInstructionCache(GetCurrentProcess(), g_patch_addr, g_patch_size);

  g_installed = true;
  char buf[64];
  snprintf(buf, sizeof(buf), "target_hook: installed at 0x%llX",
           static_cast<unsigned long long>(hit));
  LogWrite(buf);
  return true;
}

void RemoveTargetHook() {
  if (!g_installed || !g_patch_addr) {
    return;
  }
  DWORD old_prot = 0;
  if (VirtualProtect(g_patch_addr, g_patch_size, PAGE_EXECUTE_READWRITE, &old_prot)) {
    std::memcpy(g_patch_addr, g_orig_bytes, g_patch_size);
    VirtualProtect(g_patch_addr, g_patch_size, old_prot, &old_prot);
    FlushInstructionCache(GetCurrentProcess(), g_patch_addr, g_patch_size);
  }
  if (g_trampoline) {
    VirtualFree(g_trampoline, 0, MEM_RELEASE);
    g_trampoline = nullptr;
  }
  g_patch_addr = nullptr;
  g_installed = false;
  g_locked_target.store(0);
}

std::uintptr_t GetLockedTargetPtr() {
  return g_locked_target.load(std::memory_order_relaxed);
}

}  // namespace sitcom
