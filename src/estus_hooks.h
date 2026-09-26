#pragma once

namespace sitcom {

// SotFS: observe the empty-flask fail branch. Vanilla: observe the goods-use
// fail path that starts the empty-flask shake. Neither patch decrements charges.
bool EstusHooksInit();
void EstusHooksShutdown();
int EstusHooksConsumeEmpty();

}  // namespace sitcom
