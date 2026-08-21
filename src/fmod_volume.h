#pragma once

namespace sitcom {

// Hook FMOD EventSystem_Update to capture the system, then read SFX category volume.
bool FmodVolumeInit();
void FmodVolumeShutdown();

// Returns true and writes 0–1 linear gain matching the in-game Sounds slider when known.
bool FmodTryGetSfxVolume(float* out_volume);

}  // namespace sitcom
