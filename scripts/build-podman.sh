#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
podman run --rm -v "$ROOT":/src:Z -w /src docker.io/library/fedora:44 bash -lc '
  set -euo pipefail
  dnf install -y mingw64-gcc-c++ mingw64-winpthreads-static cmake make zip >/tmp/dnf.log
  cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
  cmake --build build -j"$(nproc)"
  ls -la build/dinput8.dll build/sitcom/sounds | head
  rm -rf /tmp/dsr_sitcom_pkg
  mkdir -p /tmp/dsr_sitcom_pkg/sitcom/sounds
  cp build/dinput8.dll /tmp/dsr_sitcom_pkg/
  cp sitcom/config.ini /tmp/dsr_sitcom_pkg/sitcom/
  cp sitcom/sounds/* /tmp/dsr_sitcom_pkg/sitcom/sounds/
  mkdir -p dist
  rm -f dist/DSR_Sitcom.zip
  (cd /tmp/dsr_sitcom_pkg && zip -r /src/dist/DSR_Sitcom.zip dinput8.dll sitcom)
  unzip -l /src/dist/DSR_Sitcom.zip
'
