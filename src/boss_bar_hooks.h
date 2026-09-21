#pragma once

namespace sitcom {

// SotFS: FeSceneBossHpGuage vt[4] TRACE (correlate with ActiveBossBattleId).
void BossBarHooksConfigure(bool trace);

bool BossBarHooksInit();
void BossBarHooksShutdown();
void BossBarHooksPoll();

}  // namespace sitcom
