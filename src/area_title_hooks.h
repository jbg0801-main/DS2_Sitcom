#pragma once

namespace sitcom {

// SotFS PlaceName wipe via FeSceneMapName+0x1C text id changes.
void AreaTitleHooksConfigure(bool trace);

bool AreaTitleHooksInit();
void AreaTitleHooksShutdown();
void AreaTitleHooksPoll();

int AreaTitleHooksPending();
int AreaTitleHooksConsume();

}  // namespace sitcom
