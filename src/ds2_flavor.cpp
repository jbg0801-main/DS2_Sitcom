#include "ds2_flavor.h"

#include "log.h"
#include "mem.h"

#include <psapi.h>
#include <cstdio>
#include <cstring>

namespace sitcom {
namespace {

constexpr int kSizeS103 = 0x1D76000;
constexpr int kSizeS102 = 0x20B6000;
constexpr int kSizeV112 = 0x01DB4000;
constexpr int kSizeV111 = 0x02055000;
constexpr int kSizeV102 = 0x0133e000;

Ds2Edition g_edition = Ds2Edition::Unknown;
Ds2Version g_version = Ds2Version::Unhooked;
Ds2Offsets g_offs{};
HMODULE g_mod = nullptr;
std::uintptr_t g_base = 0;
std::size_t g_size = 0;

Ds2Offsets ScholarOffsets() {
  Ds2Offsets o;
  o.player_ctrl_from_base_a = 0xD0;
  o.hp_from_player_ctrl = 0x168;
  o.hp_max_from_player_ctrl = 0x170;
  o.player_param_from_player_ctrl = 0x490;
  o.deaths_from_player_param = 0x1A4;
  o.game_state_from_base_a = 0x24AC;
  o.event_manager_from_base_a = 0x70;
  o.last_bonfire_area_from_event_manager = 0x164;
  o.loaded_enemies_from_base_a = 0x18;
  o.map_manager_from_base_a = 0x38;
  o.load_state_flag_off = 0x11C;
  return o;
}

Ds2Offsets VanillaOffsets() {
  Ds2Offsets o;
  o.player_ctrl_from_base_a = 0x74;
  o.hp_from_player_ctrl = 0xFC;
  o.hp_max_from_player_ctrl = 0x104;
  o.player_param_from_player_ctrl = 0x378;
  o.deaths_from_player_param = 0x1A0;
  o.game_state_from_base_a = 0xDEC;
  o.event_manager_from_base_a = 0x44;
  o.last_bonfire_area_from_event_manager = 0xB4;
  o.loaded_enemies_from_base_a = -1;
  o.map_manager_from_base_a = -1;
  o.load_state_flag_off = 0x1D4;
  return o;
}

const char* NameOf(Ds2Version v) {
  switch (v) {
    case Ds2Version::Sotfs102:
      return "SotFS 1.02";
    case Ds2Version::Sotfs103:
      return "SotFS 1.03";
    case Ds2Version::Vanilla102:
      return "Vanilla 1.02 (old patch)";
    case Ds2Version::Vanilla111:
      return "Vanilla 1.11";
    case Ds2Version::Vanilla112:
      return "Vanilla 1.12";
    case Ds2Version::Unsupported:
      return "unsupported DarkSoulsII.exe";
    default:
      return "unhooked";
  }
}

}  // namespace

bool DetectFlavor() {
  g_mod = GetModuleHandleW(L"DarkSoulsII.exe");
  if (!g_mod) {
    g_edition = Ds2Edition::Unknown;
    g_version = Ds2Version::Unhooked;
    return false;
  }
  MODULEINFO info{};
  if (!GetModuleInformation(GetCurrentProcess(), g_mod, &info, sizeof(info))) {
    return false;
  }
  g_base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
  g_size = static_cast<std::size_t>(info.SizeOfImage);

#if defined(_WIN64)
  g_edition = Ds2Edition::Scholar;
  g_offs = ScholarOffsets();
  if (g_size == static_cast<std::size_t>(kSizeS103)) {
    g_version = Ds2Version::Sotfs103;
  } else if (g_size == static_cast<std::size_t>(kSizeS102)) {
    g_version = Ds2Version::Sotfs102;
  } else {
    g_version = Ds2Version::Unsupported;
  }
#else
  g_edition = Ds2Edition::Vanilla;
  g_offs = VanillaOffsets();
  if (g_size == static_cast<std::size_t>(kSizeV112)) {
    g_version = Ds2Version::Vanilla112;
  } else if (g_size == static_cast<std::size_t>(kSizeV111)) {
    g_version = Ds2Version::Vanilla111;
  } else if (g_size == static_cast<std::size_t>(kSizeV102)) {
    g_version = Ds2Version::Vanilla102;
  } else {
    g_version = Ds2Version::Unsupported;
  }
#endif

  char buf[160];
  snprintf(buf, sizeof(buf), "flavor: %s module=0x%llX size=0x%llX (%s)", NameOf(g_version),
           static_cast<unsigned long long>(g_base), static_cast<unsigned long long>(g_size),
           g_version == Ds2Version::Unsupported ? "AOB still attempted" : "ok");
  LogWrite(buf);
  return true;
}

Ds2Edition Edition() { return g_edition; }
Ds2Version Version() { return g_version; }
bool IsScholar() { return g_edition == Ds2Edition::Scholar; }
bool IsVanilla() { return g_edition == Ds2Edition::Vanilla; }
const Ds2Offsets& Offsets() { return g_offs; }
HMODULE GameModule() { return g_mod; }
std::uintptr_t GameBase() { return g_base; }
std::size_t GameSize() { return g_size; }
const char* VersionName() { return NameOf(g_version); }

}  // namespace sitcom
