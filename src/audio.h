#pragma once

#include <string>

namespace sitcom {

enum class SoundCategory {
  Laugh,
  Cheer,
  Applause,
  SceneWipe,
  Boo,
  Ooh,
  Trombone,
  Bonk,
  BocDeath,  // Bed of Chaos death — loads BoCDeath*.wav
};

class Audio {
 public:
  bool Init(const std::wstring& sounds_dir, float volume);
  void Shutdown();
  // Config multiplier (0–1). Final gain = volume * game_sfx_volume.
  void SetVolume(float volume);
  // In-game Sound Effect slider as 0–1 (slider/10).
  void SetGameSfxVolume(float game_sfx_volume);
  void Play(SoundCategory category);

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace sitcom
