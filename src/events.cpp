#include "events.h"

#include "area_title_hooks.h"
#include "ds2_flavor.h"
#include "item_hooks.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <unordered_set>

namespace sitcom {
namespace {

// Tentative TAE IDs. DS1 leftovers kept; DS2 goods/estus band probed in logs.
bool IsPickupAnim(std::int32_t anim) { return anim == 7520 || anim == 7522; }
bool IsLadderFallAnim(std::int32_t anim) {
  return anim == 1560 || (anim >= 7050 && anim <= 7052) || (anim >= 3100 && anim <= 3120);
}
bool IsFailCastAnim(std::int32_t anim) {
  return anim == 6299 || anim == 6399 || anim == 2010 || anim == 2011;
}
bool IsEmptyEstusAnim(std::int32_t anim) {
  // DSR-like + common DS2 drink / empty-flask candidates (confirm via probe logs).
  return anim == 7588 || anim == 7589 || anim == 5000 || anim == 5001 || anim == 5002 ||
         anim == 5010 || anim == 5011 || anim == 5100 || anim == 5101 || anim == 6000 ||
         anim == 6001 || anim == 16 || anim == 17 || anim == 18 || anim == 19;
}
bool IsGoodsProbeAnim(std::int32_t anim) {
  return (anim >= 7400 && anim <= 7600) || (anim >= 5000 && anim <= 5200) ||
         (anim >= 6000 && anim <= 6200) || (anim >= 1 && anim <= 40);
}
bool IsLockedUseAnim(std::int32_t anim) { return anim == 7510 || anim == 2020 || anim == 2021; }

bool AnimEntered(const GameSnapshot& snap, const GameSnapshot& prev, bool (*pred)(std::int32_t)) {
  return snap.anim_valid && prev.anim_valid && pred(snap.current_anim) && !pred(prev.current_anim);
}

bool TryFailLaugh(std::uint64_t now, std::uint64_t* last_ms, float cooldown_sec, Audio& audio,
                  const char* label) {
  const auto cd = static_cast<std::uint64_t>(cooldown_sec * 1000.0f);
  if (now - *last_ms < cd) {
    return false;
  }
  LogWrite(std::string("event: fail laugh — ") + label);
  audio.Play(SoundCategory::Laugh);
  *last_ms = now;
  return true;
}

}  // namespace

void EventDetector::Reset() {
  has_prev_ = false;
  prev_ = {};
  last_laugh_hit_ms_ = 0;
  last_ooh_ms_ = 0;
  last_fail_laugh_ms_ = 0;
  flags_seeded_ = false;
  gameplay_seeded_ms_ = 0;
  stable_area_id_ = 0;
  load_edge_armed_ = false;
  deaths_at_load_start_ = 0;
  last_wipe_ms_ = 0;
}

void EventDetector::Update(const GameSnapshot& snap, const Config& cfg, Audio& audio) {
  if (!cfg.enabled) {
    return;
  }

  const auto now = static_cast<std::uint64_t>(GetTickCount64());

  // PlaceName wipe — FeSceneMapName+0x1C place-id edges (Betwixt, Majula, …).
  AreaTitleHooksPoll();
  if (cfg.wipe_on_area_title && !snap.on_title_screen && AreaTitleHooksPending() > 0 &&
      (now - last_wipe_ms_) >= 1200) {
    const int titles = AreaTitleHooksConsume();
    if (titles > 0) {
      LogWrite("event: area title card (MapName+0x1C x" + std::to_string(titles) + ")");
      audio.Play(SoundCategory::SceneWipe);
      last_wipe_ms_ = now;
    }
  }

  const int got = ItemHooksConsumeAcquired();
  const int suppressed = ItemHooksConsumeSuppressed();
  const int item_id = ItemHooksLastItemId();
  bool ooh_from_itemget = false;
  if (cfg.ooh_on_item_get && snap.in_gameplay && !snap.on_title_screen && flags_seeded_ &&
      gameplay_seeded_ms_ != 0 && (now - gameplay_seeded_ms_) >= 2500 && got == 1 &&
      suppressed == 0 && item_id != 0) {
    ooh_from_itemget = true;
  } else if (got > 0 && got <= 4) {
    char buf[96];
    snprintf(buf, sizeof(buf), "event: ItemGive skip (x%d id=0x%X)", got,
             static_cast<unsigned>(item_id));
    LogWrite(buf);
  }

  if (!has_prev_) {
    prev_ = snap;
    has_prev_ = true;
    if (snap.in_gameplay) {
      flags_seeded_ = true;
      gameplay_seeded_ms_ = now;
    }
    return;
  }

  if (cfg.ooh_on_item_get && snap.in_gameplay && !snap.on_title_screen) {
    const bool from_anim = AnimEntered(snap, prev_, IsPickupAnim);
    if (from_anim || ooh_from_itemget) {
      const auto cd = static_cast<std::uint64_t>(cfg.ooh_cooldown_seconds * 1000.0f);
      if (now - last_ooh_ms_ >= cd) {
        if (from_anim) {
          LogWrite("event: item pickup (anim=" + std::to_string(snap.current_anim) + ")");
        } else {
          LogWrite("event: item pickup (ItemGive id=0x" + std::to_string(item_id) + ")");
        }
        audio.Play(SoundCategory::Ooh);
        last_ooh_ms_ = now;
      }
    }
  }

  if (snap.on_title_screen || snap.game_state == kGameStateMainMenu) {
    stable_area_id_ = 0;
    load_edge_armed_ = false;
  }

  if (!snap.in_gameplay) {
    if (prev_.in_gameplay) {
      flags_seeded_ = false;
      gameplay_seeded_ms_ = 0;
    }
    prev_ = snap;
    return;
  }

  if (!flags_seeded_) {
    flags_seeded_ = true;
    gameplay_seeded_ms_ = now;
    prev_ = snap;
    LogWrite("event: seeded flag baselines in gameplay");
    return;
  }

  if (snap.player_valid && prev_.player_valid && prev_.in_gameplay) {
    const bool max_hp_changed = snap.player_max_hp != prev_.player_max_hp;
    if (!max_hp_changed && snap.player_hp < prev_.player_hp) {
      if (snap.player_hp <= 0) {
        if (cfg.laugh_on_death) {
          LogWrite("event: player death");
          audio.Play(SoundCategory::Laugh);
        }
      } else if (cfg.laugh_on_hit) {
        const auto cooldown_ms = static_cast<std::uint64_t>(cfg.laugh_hit_seconds * 1000.0f);
        if (now - last_laugh_hit_ms_ >= cooldown_ms) {
          LogWrite("event: player hit");
          audio.Play(SoundCategory::Laugh);
          last_laugh_hit_ms_ = now;
        }
      }
    } else if (snap.deaths > prev_.deaths) {
      if (cfg.laugh_on_death) {
        LogWrite("event: death counter increased");
        audio.Play(SoundCategory::Laugh);
      }
    }
  }

  if (cfg.cheer_on_boss_bar) {
    std::unordered_set<std::int32_t> prev_chr(prev_.cheer_chr_ids.begin(), prev_.cheer_chr_ids.end());
    for (const auto id : snap.cheer_chr_ids) {
      if (prev_chr.count(id) == 0) {
        LogWrite("event: boss fight start chr=" + std::to_string(id) +
                 " defeat_flag=" + std::to_string(snap.boss_defeat_flag));
        audio.Play(SoundCategory::Cheer);
        break;
      }
    }
  }

  if (cfg.applause_on_boss_death) {
    std::unordered_set<std::int32_t> prev_def(prev_.defeat_flags_on.begin(),
                                              prev_.defeat_flags_on.end());
    bool clapped = false;
    for (const auto id : snap.defeat_flags_on) {
      if (prev_def.count(id) != 0) {
        continue;
      }
      LogWrite("event: boss defeated flag=" + std::to_string(id));
      audio.Play(SoundCategory::Applause);
      clapped = true;
      break;
    }
    if (!clapped) {
      std::unordered_set<std::int32_t> prev_kills(prev_.boss_kills_on.begin(),
                                                  prev_.boss_kills_on.end());
      for (const auto off : snap.boss_kills_on) {
        if (prev_kills.count(off) != 0) {
          continue;
        }
        LogWrite("event: boss defeated kill-count off=0x" + std::to_string(off));
        audio.Play(SoundCategory::Applause);
        break;
      }
    }
  }

  if (snap.anim_valid && prev_.anim_valid) {
    if (cfg.laugh_on_empty_flask && snap.current_anim != prev_.current_anim) {
      LogWrite("probe: anim " + std::to_string(prev_.current_anim) + "→" +
               std::to_string(snap.current_anim));
    }
    if (cfg.laugh_on_empty_flask && AnimEntered(snap, prev_, IsEmptyEstusAnim)) {
      TryFailLaugh(now, &last_fail_laugh_ms_, cfg.laugh_hit_seconds, audio, "empty flask");
    }
    if (cfg.laugh_on_locked_door && AnimEntered(snap, prev_, IsLockedUseAnim)) {
      TryFailLaugh(now, &last_fail_laugh_ms_, cfg.laugh_hit_seconds, audio, "locked/invalid use");
    }
    if (cfg.laugh_on_no_spell && AnimEntered(snap, prev_, IsFailCastAnim)) {
      TryFailLaugh(now, &last_fail_laugh_ms_, cfg.laugh_hit_seconds, audio, "no spell");
    }
    if (cfg.laugh_on_ladder_fall && AnimEntered(snap, prev_, IsLadderFallAnim)) {
      TryFailLaugh(now, &last_fail_laugh_ms_, cfg.laugh_hit_seconds, audio, "ladder fall");
    }
  }

  prev_ = snap;
}

}  // namespace sitcom
