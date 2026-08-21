# DSR Sitcom

Sitcom canned-audience overlay for **Dark Souls Remastered** (PC).

Plays local WAV clips when:

- the player takes damage or dies (laughter)
- a boss fight starts / HP bar comes up via vanilla event flags (cheering; again on fog re-entry)
- that boss’s defeated flag flips (applause)
- the area/world number changes (scene wipe — proxy for the area title card)

## Install

1. Copy `dinput8.dll` and the `sitcom/` folder into the game directory (next to `DarkSoulsRemastered.exe`).
2. Launch the game normally.
3. **Linux / Proton** launch options:

```text
WINEDLLOVERRIDES="dinput8.dll=n,b" %command%
```

4. Put your WAVs in `sitcom/sounds/` using the names below.
5. Tweak `sitcom/config.ini` as needed. Set `log=true` to write `sitcom/sitcom.log`.

**Offline / single-player recommended.** Any injected DLL can interact badly with Softs / online play.

If another mod already ships `dinput8.dll`, only one can own that filename unless you chain-load (not supported in v1).

## Sound files

Preferred format: **WAV, PCM signed 16-bit, 44100 Hz** (48000 OK). Stereo is fine.

Keep hit laughs short when you can; long cheer/applause clips will overlap if fights chain quickly (cooldowns only apply to hit-laughs).

| Prefix / name | Event |
| --- | --- |
| `laugh.wav` / `laugh_XX.wav` | Hit / death |
| `cheer.wav` / `cheer_XX.wav` | Boss bar |
| `applause.wav` / `applause_XX.wav` | Boss death |
| `scene_wipe.wav` / `scene_wipe_XX.wav` | Area change |

## Build (Linux → Windows DLL)

Requires MinGW-w64 (`x86_64-w64-mingw32-g++`) and CMake.

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
cmake --build build -j
```

Output: `build/dinput8.dll` plus a copied `build/sitcom/`.

Without a host MinGW install, build inside Podman/Docker:

```bash
podman run --rm -v "$PWD":/src -w /src docker.io/library/fedora:44 \
  bash -lc 'dnf install -y mingw64-gcc-c++ mingw64-winpthreads-static cmake make &&
            cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake &&
            cmake --build build -j'
```

## How it works

`dinput8.dll` proxies `DirectInput8Create` to the system DLL and starts a worker thread that:

1. Resolves `BaseB` / `BaseE` via AOB scans (Phokz / community tables)
2. Optionally installs a tiny lock-on capture hook for boss HP
3. Polls ~20 Hz and plays sounds through **miniaudio**

See [tools/POINTERS.md](tools/POINTERS.md) for pointer notes.
