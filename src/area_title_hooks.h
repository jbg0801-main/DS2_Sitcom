#pragma once

namespace sitcom {

// Hooks FeSubStateTitleInformation PlaceName show (SotFS).
bool AreaTitleHooksInit();
void AreaTitleHooksShutdown();

// Poll captured TitleInformation +0x10 for idle→show transitions (Majula etc.).
void AreaTitleHooksPoll();

int AreaTitleHooksPending();
int AreaTitleHooksConsume();

}  // namespace sitcom
