#include "game_state.h"

#include "event_flags.h"
#include "log.h"

#include <windows.h>
#include <psapi.h>

#include <cstring>
#include <string>
#include <unordered_set>

namespace sitcom {
namespace {

EventFlags g_event_flags;

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

template <typename T>
T ReadT(std::uintptr_t ptr, T fallback = T{}) {
  return IsReadable(ptr) ? *reinterpret_cast<T*>(ptr) : fallback;
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

std::uintptr_t ResolveRipMov(std::uintptr_t insn) {
  const auto rel = *reinterpret_cast<std::int32_t*>(insn + 3);
  return insn + 7 + rel;
}

// cheer_flag → defeat_flag. Cheer = vanilla EMEVD "boss bar up" flag
// (e.g. Asylum SetEventFlag 11815396 when Display Boss Health Bar enables).
struct BossPair {
  std::int32_t cheer_flag;
  std::int32_t defeat_flag;
};

constexpr BossPair kBossPairs[] = {
    {11815396, 16},         // Asylum Demon
    {11815382, 11810900},   // Stray Demon
    {11805392, 15},         // Gwyn
    {11705392, 14},         // Seath
    {11705383, 14},         // Seath (crystal cave approach)
    {11605392, 13},         // Four Kings
    {11605382, 13},         // Four Kings (alt)
    {11515382, 11510900},   // Gwyndolin
    {11515392, 12},         // Ornstein & Smough
    {11515396, 12},         // O&S phase / super
    {11505392, 11},         // Iron Golem
    {11415392, 10},         // Bed of Chaos
    {11415372, 11410900},   // Ceaseless Discharge
    {11415382, 11410901},   // Centipede Demon
    {11415342, 11410410},   // Demon Firesage
    {11405392, 9},          // Quelaag
    {11315392, 7},          // Nito
    {11305392, 6},          // Pinwheel
    {11215003, 11210000},   // Sanctuary Guardian
    {11215013, 11210001},   // Artorias
    {11215023, 11210002},   // Manus
    {11215063, 11210005},   // Kalameet
    {11205392, 5},          // Sif
    {11205382, 11200900},   // Moonlight Butterfly
    {11105392, 4},          // Priscilla
    {11105398, 4},          // Priscilla (alt)
    {11015392, 3},          // Bell Gargoyles
    {11015396, 3},          // Bell Gargoyles (2nd)
    {11015384, 11010901},   // Taurus Demon
    {11015372, 11010902},   // Capra Demon
    {11005392, 2},          // Gaping Dragon
};

}  // namespace

bool GameState::ResolveBases() {
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

  const std::uint8_t pat_b[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x45, 0x33, 0xED, 0x48, 0x8B, 0xF1,
                                0x48, 0x85, 0xC0};
  const char mask_b[] = "xxx????xxxxxxxxx";
  const auto hit_b = PatternScan(base, size, pat_b, mask_b);
  if (!hit_b) {
    LogWrite("game_state: BaseB pattern not found");
    return false;
  }
  base_b_ptr_ = ResolveRipMov(hit_b);

  const std::uint8_t pat_e[] = {0x48, 0x8B, 0x05, 0,    0,    0,    0,    0x48, 0x8B, 0x88,
                                0x98, 0x0B, 0x00, 0x00, 0x8B, 0x41, 0x3C, 0xC3};
  const char mask_e[] = "xxx????xxxxxxxxxxx";
  const auto hit_e = PatternScan(base, size, pat_e, mask_e);
  if (hit_e) {
    base_e_ptr_ = ResolveRipMov(hit_e);
  } else {
    LogWrite("game_state: BaseE pattern not found (area context disabled)");
    base_e_ptr_ = 0;
  }

  // MenuMan — Display Banner / "Message" at +0x104 (8 = Current Location title card)
  // Prefer RemasterCETable short AOB (works across more builds); fall back to DSR-Gadget long form.
  // 48 8B 05 ?? ?? ?? ?? 89 88 28 08 00 00 85 C9
  const std::uint8_t pat_m_short[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x89, 0x88, 0x28,
                                      0x08, 0x00, 0x00, 0x85, 0xC9};
  const char mask_m_short[] = "xxx????xxxxxxxx";
  auto hit_m = PatternScan(base, size, pat_m_short, mask_m_short);
  if (!hit_m) {
    const std::uint8_t pat_m_long[] = {
        0x48, 0x8B, 0x05, 0,    0,    0,    0,    0x89, 0x88, 0x28, 0x08, 0x00, 0x00, 0x85,
        0xC9, 0,    0,    0xC7, 0x80, 0x34, 0x08, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xC3};
    const char mask_m_long[] = "xxx????xxxxxxx??xxxxxxxxxxxx";
    hit_m = PatternScan(base, size, pat_m_long, mask_m_long);
  }
  if (hit_m) {
    menu_man_ptr_ = ResolveRipMov(hit_m);
  } else {
    LogWrite("game_state: MenuMan pattern not found (title-card wipe disabled)");
    menu_man_ptr_ = 0;
  }

  // WorldChrBase — player chr / model id (DSR-Gadget WorldChrBaseAOB).
  const std::uint8_t pat_w[] = {0x48, 0x8B, 0x05, 0,    0,    0,    0,    0x48, 0x8B, 0x48,
                                0x68, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0,    0,    0,    0,
                                0x48, 0x39, 0x5E, 0x10, 0x0F, 0x84, 0,    0,    0,    0, 0x48};
  const char mask_w[] = "xxx????xxxxxxxxx????xxxxxx????x";
  const auto hit_w = PatternScan(base, size, pat_w, mask_w);
  if (hit_w) {
    world_chr_ptr_ = ResolveRipMov(hit_w);
  } else {
    LogWrite("game_state: WorldChrBase not found (lock model limited)");
    world_chr_ptr_ = 0;
  }

  // LockTGTBase — RemasterCETable findit4.
  const std::uint8_t pat_l[] = {0x48, 0x8B, 0x0D, 0, 0, 0, 0, 0x89, 0x99, 0, 0, 0, 0, 0x4C, 0x89,
                                0x6D, 0x58};
  const char mask_l[] = "xxx????xx????xxxx";
  const auto hit_l = PatternScan(base, size, pat_l, mask_l);
  if (hit_l) {
    // RIP-relative is mov rcx, [rip+rel] at offset 0: 48 8B 0D xx xx xx xx
    const auto rel = *reinterpret_cast<std::int32_t*>(hit_l + 3);
    lock_tgt_ptr_ = hit_l + 7 + rel;
  } else {
    LogWrite("game_state: LockTGTBase not found");
    lock_tgt_ptr_ = 0;
  }

  char buf[220];
  snprintf(buf, sizeof(buf),
           "game_state: BaseB*=0x%llX BaseE*=0x%llX MenuMan*=0x%llX WorldChr*=0x%llX LockTGT*=0x%llX",
           static_cast<unsigned long long>(base_b_ptr_),
           static_cast<unsigned long long>(base_e_ptr_),
           static_cast<unsigned long long>(menu_man_ptr_),
           static_cast<unsigned long long>(world_chr_ptr_),
           static_cast<unsigned long long>(lock_tgt_ptr_));
  LogWrite(buf);
  return base_b_ptr_ != 0;
}

bool GameState::Init() {
  ready_ = ResolveBases();
  if (ready_) {
    if (!g_event_flags.Init()) {
      LogWrite("game_state: event flags unavailable (boss cheer/applause disabled)");
    }
  }
  return ready_;
}

void GameState::Shutdown() {
  g_event_flags.Shutdown();
  ready_ = false;
  base_b_ptr_ = 0;
  base_e_ptr_ = 0;
  menu_man_ptr_ = 0;
  world_chr_ptr_ = 0;
  lock_tgt_ptr_ = 0;
}

GameSnapshot GameState::Read() {
  GameSnapshot s{};
  if (!ready_ || !base_b_ptr_) {
    return s;
  }

  if (!IsReadable(base_b_ptr_)) {
    return s;
  }
  const auto base_b = ReadT<std::uintptr_t>(base_b_ptr_);
  if (!base_b || !IsReadable(base_b)) {
    return s;
  }

  const auto chr_stat = ReadT<std::uintptr_t>(base_b + 0x10);
  if (!chr_stat || !IsReadable(chr_stat)) {
    return s;
  }

  s.player_hp = ReadT<std::int32_t>(chr_stat + 0x14);
  s.player_max_hp = ReadT<std::int32_t>(chr_stat + 0x1C);
  s.deaths = ReadT<std::int32_t>(base_b + 0x98);
  // Allow hp > max (load glitches / buffs); reject obvious garbage.
  s.player_valid = (s.player_max_hp > 0 && s.player_max_hp <= 99999 && s.player_hp >= 0 &&
                    s.player_hp <= 99999);

  if (base_e_ptr_ && IsReadable(base_e_ptr_)) {
    const auto base_e = ReadT<std::uintptr_t>(base_e_ptr_);
    if (base_e && IsReadable(base_e + 0xA22)) {
      s.area_number = ReadT<std::uint8_t>(base_e + 0xA22);
      s.world_number = ReadT<std::uint8_t>(base_e + 0xA23);
      // 255 = unloaded / menu / load screen sentinel in practice.
      s.area_valid = (s.area_number != 255 && s.world_number != 255);
    }
  }

  // In-world: real map loaded. Player vitals may be briefly invalid during loads.
  s.in_gameplay = s.area_valid && s.player_valid;

  // Display Banner ID (area title card = 8 Current Location).
  // Title screen flag at MenuMan+0x80 (DS1 Overhaul / community).
  if (menu_man_ptr_ && IsReadable(menu_man_ptr_)) {
    const auto menu_man = ReadT<std::uintptr_t>(menu_man_ptr_);
    if (menu_man && IsReadable(menu_man + 0x104)) {
      s.banner_message = ReadT<std::uint8_t>(menu_man + 0x104);
      s.banner_valid = true;
    }
    if (menu_man && IsReadable(menu_man + 0x80)) {
      s.on_title_screen = (ReadT<std::int32_t>(menu_man + 0x80) == 1);
    }
  }

  // Boss fights via event flags (EMEVD), not lock-on.
  if (g_event_flags.Ready() && s.in_gameplay) {
    std::unordered_set<std::int32_t> cheer_seen;
    std::unordered_set<std::int32_t> defeat_seen;
    for (const auto& pair : kBossPairs) {
      bool cheer = false;
      bool defeated = false;
      if (!g_event_flags.ReadFlag(pair.cheer_flag, &cheer)) {
        continue;
      }
      if (!g_event_flags.ReadFlag(pair.defeat_flag, &defeated)) {
        continue;
      }
      if (cheer && cheer_seen.insert(pair.cheer_flag).second) {
        s.cheer_flags_on.push_back(pair.cheer_flag);
      }
      if (defeated && defeat_seen.insert(pair.defeat_flag).second) {
        s.defeat_flags_on.push_back(pair.defeat_flag);
      }
      if (cheer && !defeated && !s.boss_fight_active) {
        s.boss_fight_active = true;
        s.boss_defeat_flag = pair.defeat_flag;
        s.boss_cheer_flag = pair.cheer_flag;
      }
    }
  }

  // Lock-on flag (RemasterCETable LockTGTBase+0x1431).
  if (lock_tgt_ptr_ && IsReadable(lock_tgt_ptr_)) {
    const auto lock_man = ReadT<std::uintptr_t>(lock_tgt_ptr_);
    if (lock_man && IsReadable(lock_man + 0x1431)) {
      s.lock_on = ReadT<std::uint8_t>(lock_man + 0x1431) != 0;
    }
  }

  // Estus count unknown until a goods inventory reader lands.
  s.estus_count = -1;

  // Player anims (WorldChr → ChrData1 = *(WorldChr)+0x68):
  //   CurrentAnim try DSR-Gadget (*(*(*(ChrData1+0x68)+0x48)+0x80)) then
  //   RemasterCETable (*(*(*(ChrData1+0x48)+0x48)+0x80)) — TAE id.
  //   StayAnimID (CE): *(*(*(ChrData1+0x48)+0x20)+0x2A0 / 0x498)
  if (world_chr_ptr_ && IsReadable(world_chr_ptr_)) {
    const auto world_chr = ReadT<std::uintptr_t>(world_chr_ptr_);
    if (world_chr && IsReadable(world_chr + 0x68)) {
      const auto chr_data1 = ReadT<std::uintptr_t>(world_chr + 0x68);
      if (chr_data1 && IsReadable(chr_data1)) {
        auto try_current_anim = [&](std::uintptr_t mid_off) -> bool {
          const auto anim_mid = ReadT<std::uintptr_t>(chr_data1 + mid_off);
          if (!anim_mid || !IsReadable(anim_mid + 0x48)) {
            return false;
          }
          const auto anim_struct = ReadT<std::uintptr_t>(anim_mid + 0x48);
          if (!anim_struct || !IsReadable(anim_struct + 0x80)) {
            return false;
          }
          s.current_anim = ReadT<std::int32_t>(anim_struct + 0x80, -1);
          s.anim_valid = true;
          return true;
        };
        // Prefer CE path (more stable in playtests); fall back to Gadget.
        if (!try_current_anim(0x48)) {
          try_current_anim(0x68);
        }

        const auto stay_a = ReadT<std::uintptr_t>(chr_data1 + 0x48);
        if (stay_a && IsReadable(stay_a + 0x20)) {
          const auto stay_b = ReadT<std::uintptr_t>(stay_a + 0x20);
          if (stay_b) {
            if (IsReadable(stay_b + 0x2A0)) {
              s.stay_anim_upper = ReadT<std::int32_t>(stay_b + 0x2A0, -1);
              s.anim_valid = true;
            }
            if (IsReadable(stay_b + 0x498)) {
              s.stay_anim_lower = ReadT<std::int32_t>(stay_b + 0x498, -1);
              s.anim_valid = true;
            }
          }
        }
      }
    }
  }

  return s;
}

}  // namespace sitcom
