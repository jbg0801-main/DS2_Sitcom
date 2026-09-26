#pragma once

namespace sitcom {

// PlaceName wipe via FeSceneMapName text id changes.
// SotFS displayed id is +0x1C. Vanilla 1.12 displayed id is +0x10.
void AreaTitleHooksConfigure(bool trace);

bool AreaTitleHooksInit();
void AreaTitleHooksShutdown();
void AreaTitleHooksPoll();

int AreaTitleHooksPending();
int AreaTitleHooksConsume();

}  // namespace sitcom
