#pragma once

namespace sitcom {

// Hook FMOD EventSystem_Update to capture the system, then read SFX category volume.
bool FmodVolumeInit();
void FmodVolumeShutdown();

// Writes the in-game Sounds slider as a 0 to 1 gain. Returns false until that read exists.
bool FmodTryGetSfxVolume(float* out_volume);

}  // namespace sitcom
