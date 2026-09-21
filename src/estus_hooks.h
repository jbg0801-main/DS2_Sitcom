#pragma once

namespace sitcom {

// SotFS: hooks Estus empty-flask fail branch (shake anim path), not the decrement.
bool EstusHooksInit();
void EstusHooksShutdown();
int EstusHooksConsumeEmpty();

}  // namespace sitcom
