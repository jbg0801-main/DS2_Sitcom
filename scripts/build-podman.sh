#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
podman run --rm -v "$ROOT":/src:Z -w /src docker.io/library/fedora:44 bash -lc '
  set -euo pipefail
  dnf install -y mingw64-gcc-c++ mingw64-winpthreads-static \
                 mingw32-gcc-c++ mingw32-winpthreads-static cmake make zip >/tmp/dnf.log

  cmake -S . -B build/sotfs -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
  cmake --build build/sotfs -j"$(nproc)"
  cmake -S . -B build/vanilla -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686-toolchain.cmake
  cmake --build build/vanilla -j"$(nproc)"

  ls -la build/sotfs/dinput8.dll build/vanilla/dinput8.dll

  pack() {
    local name="$1" dll="$2"
    rm -rf /tmp/${name}_pkg
    mkdir -p /tmp/${name}_pkg
    cp "$dll" /tmp/${name}_pkg/
    cp -a sitcom /tmp/${name}_pkg/
    mkdir -p dist
    rm -f dist/${name}.zip
    (cd /tmp/${name}_pkg && zip -r /src/dist/${name}.zip dinput8.dll sitcom)
    unzip -l /src/dist/${name}.zip
  }

  pack DS2SotFS_Sitcom build/sotfs/dinput8.dll
  pack DS2_Sitcom build/vanilla/dinput8.dll
'
