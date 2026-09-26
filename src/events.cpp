#include "events.h"

#include "area_title_hooks.h"
#include "boss_bar_hooks.h"
#include "ds2_flavor.h"
#include "estus_hooks.h"
#include "item_hooks.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <string>

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
  // SotFS empty-flask shake (live probe): 180200 → 900 → 180202 (shake/head-shake).
  // Filled chug is 900 → 180201 → 920. Charge-helper fail branch is never entered.
  return anim == 180202;
}
bool IsGoodsProbeAnim(std::int32_t anim) {
  return (anim >= 7400 && anim <= 7600) || (anim >= 5000 && anim <= 5200) ||
         (anim >= 6000 && anim <= 6200) || (anim >= 1 && anim <= 40);
}
bool IsLockedUseAnim(std::int32_t anim) { return anim == 7510 || anim == 2020 || anim == 2021; }

// Backstab / riposte band seen in probe logs the poll after a false hit laugh.
bool IsCriticalAnim(std::int32_t anim) {
  return (anim >= 160000 && anim <= 160999) || (anim >= 190000 && anim <= 190999);
}

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
  last_death_laugh_ms_ = 0;
  cheer_latched_battle_id_ = 0;
  pending_applause_battle_id_ = 0;
  pending_applause_ms_ = 0;
  last_loot_ms_ = 0;
  last_applause_ms_ = 0;
  pending_hit_ = false;
  pending_hit_from_hp_ = 0;
  applause_baselined_ = false;
  seen_defeat_flags_.clear();
  seen_kill_offsets_.clear();
}

void EventDetector::Update(const GameSnapshot& snap, const Config& cfg, Audio& audio) {
  if (!cfg.enabled) {
    return;
  }

  const auto now = static_cast<std::uint64_t>(GetTickCount64());

  // PlaceName wipe — FeSceneMapName+0x1C place-id edges (Betwixt, Majula, …).
  AreaTitleHooksPoll();
  BossBarHooksPoll();
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
  if (got > 0 && suppressed == 0 && item_id != 0) {
    last_loot_ms_ = now;
  }
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
    cheer_latched_battle_id_ = 0;
    pending_applause_battle_id_ = 0;
    pending_applause_ms_ = 0;
    pending_hit_ = false;
    applause_baselined_ = false;
    seen_defeat_flags_.clear();
    seen_kill_offsets_.clear();
  }

  // Death often drives HP negative in one poll, which clears player_valid / in_gameplay
  // before the in-gameplay HP branch runs. Catch it on that edge (and deaths++).
  if (prev_.in_gameplay && cfg.laugh_on_death) {
    const bool hp_killed = prev_.player_hp > 0 && snap.player_hp <= 0;
    const bool deaths_up = snap.deaths > prev_.deaths;
    if ((hp_killed || deaths_up) && (now - last_death_laugh_ms_) >= 2500) {
      LogWrite(hp_killed ? "event: player death" : "event: death counter increased");
      audio.Play(SoundCategory::Laugh);
      last_death_laugh_ms_ = now;
    }
  }

  if (!snap.in_gameplay) {
    if (prev_.in_gameplay) {
      flags_seeded_ = false;
      gameplay_seeded_ms_ = 0;
    }
    pending_hit_ = false;
    prev_ = snap;
    return;
  }

  // Defeat flags survive a gameplay blip. Baseline only the first in-game sample
  // after the title screen — a flag that appears when gameplay returns still applauds.
  if (cfg.applause_on_boss_death) {
    if (!applause_baselined_) {
      seen_defeat_flags_.insert(snap.defeat_flags_on.begin(), snap.defeat_flags_on.end());
      seen_kill_offsets_.insert(snap.boss_kills_on.begin(), snap.boss_kills_on.end());
      applause_baselined_ = true;
    } else {
      // While a fight is still running, the death sting covers an immediate clap.
      // Hold it for the battle-id clear a few seconds later.
      const bool hold_for_fight_end =
          cheer_latched_battle_id_ > 0 || snap.active_boss_battle_id > 0;
      bool clapped = false;
      for (const auto id : snap.defeat_flags_on) {
        if (!seen_defeat_flags_.insert(id).second || clapped) {
          continue;
        }
        if (hold_for_fight_end) {
          LogWrite("event: boss defeated flag=" + std::to_string(id) + " (hold for fight end)");
          continue;
        }
        LogWrite("event: boss defeated flag=" + std::to_string(id));
        audio.Play(SoundCategory::Applause);
        last_applause_ms_ = now;
        clapped = true;
      }
      for (const auto off : snap.boss_kills_on) {
        if (!seen_kill_offsets_.insert(off).second || clapped) {
          continue;
        }
        if (hold_for_fight_end) {
          LogWrite("event: boss defeated kill-count off=0x" + std::to_string(off) +
                   " (hold for fight end)");
          continue;
        }
        LogWrite("event: boss defeated kill-count off=0x" + std::to_string(off));
        audio.Play(SoundCategory::Applause);
        last_applause_ms_ = now;
        clapped = true;
      }
    }
  }

  if (!flags_seeded_) {
    flags_seeded_ = true;
    gameplay_seeded_ms_ = now;
    prev_ = snap;
    LogWrite("event: seeded flag baselines in gameplay");
    return;
  }

  if (pending_hit_) {
    const bool crit = snap.anim_valid && IsCriticalAnim(snap.current_anim);
    const bool still_down = snap.player_valid && snap.player_hp < pending_hit_from_hp_;
    pending_hit_ = false;
    if (!crit && still_down && cfg.laugh_on_hit) {
      const auto cooldown_ms = static_cast<std::uint64_t>(cfg.laugh_hit_seconds * 1000.0f);
      if (now - last_laugh_hit_ms_ >= cooldown_ms) {
        LogWrite("event: player hit");
        audio.Play(SoundCategory::Laugh);
        last_laugh_hit_ms_ = now;
      }
    }
  }

  if (snap.player_valid && prev_.player_valid && prev_.in_gameplay) {
    const bool max_hp_changed = snap.player_max_hp != prev_.player_max_hp;
    if (!max_hp_changed && snap.player_hp < prev_.player_hp) {
      if (snap.player_hp <= 0) {
        // Usually handled on the gameplay-exit edge; keep as in-game backup.
        if (cfg.laugh_on_death && (now - last_death_laugh_ms_) >= 2500) {
          LogWrite("event: player death");
          audio.Play(SoundCategory::Laugh);
          last_death_laugh_ms_ = now;
        }
      } else if (cfg.laugh_on_hit && !(snap.anim_valid && IsCriticalAnim(snap.current_anim))) {
        pending_hit_ = true;
        pending_hit_from_hp_ = prev_.player_hp;
      }
    } else if (snap.deaths > prev_.deaths) {
      if (cfg.laugh_on_death && (now - last_death_laugh_ms_) >= 2500) {
        LogWrite("event: death counter increased");
        audio.Play(SoundCategory::Laugh);
        last_death_laugh_ms_ = now;
      }
    }
  }

  if (cfg.cheer_on_boss_bar) {
    const auto id = snap.active_boss_battle_id;
    if (id <= 0) {
      // Fog locks for the fight, so id→0 with the player still up is the kill.
      // Require nearby loot so walking out (or a flicker) does not applaud.
      if (cheer_latched_battle_id_ > 0 && snap.player_hp > 0) {
        pending_applause_battle_id_ = cheer_latched_battle_id_;
        pending_applause_ms_ = now;
      }
      cheer_latched_battle_id_ = 0;
    } else if (cheer_latched_battle_id_ == 0) {
      if (prev_.active_boss_battle_id <= 0) {
        LogWrite("event: boss fight start battle_id=" + std::to_string(id));
        audio.Play(SoundCategory::Cheer);
      }
      cheer_latched_battle_id_ = id;
    } else if (id != cheer_latched_battle_id_) {
      LogWrite("event: boss fight start battle_id=" + std::to_string(id) + " (switch)");
      audio.Play(SoundCategory::Cheer);
      cheer_latched_battle_id_ = id;
    }
  }

  if (cfg.applause_on_boss_death && pending_applause_battle_id_ != 0) {
    const bool died = snap.player_hp <= 0 || snap.deaths > prev_.deaths;
    const bool loot_near =
        last_loot_ms_ != 0 && last_loot_ms_ + 4000 >= pending_applause_ms_ &&
        pending_applause_ms_ + 4000 >= last_loot_ms_;
    if (died || now - pending_applause_ms_ > 4000) {
      pending_applause_battle_id_ = 0;
      pending_applause_ms_ = 0;
    } else if (loot_near) {
      LogWrite("event: boss defeated battle_id=" + std::to_string(pending_applause_battle_id_));
      audio.Play(SoundCategory::Applause);
      last_applause_ms_ = now;
      pending_applause_battle_id_ = 0;
      pending_applause_ms_ = 0;
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

  // Primary empty-flask signal (SotFS): Estus chug with charge byte already 0.
  if (cfg.laugh_on_empty_flask && EstusHooksConsumeEmpty() > 0) {
    TryFailLaugh(now, &last_fail_laugh_ms_, cfg.laugh_hit_seconds, audio, "empty flask");
  }

  prev_ = snap;
}

}  // namespace sitcom
