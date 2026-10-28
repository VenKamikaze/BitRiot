#!/usr/bin/env bash
# Setup script for a Claude Code cloud environment (Ubuntu 24.04).
#
# Installs everything needed to build BitRiot and run the headless test harness:
# SDL2 (+mixer/ttf/image), OpenGL headers, FreeType, GoogleTest, and RmlUi 4.3
# built as a shared library into /usr/local (Ubuntu ships no RmlUi package).
#
# Self-contained on purpose - it does not need the BitRiot checkout, so it can
# be pasted straight into the environment's "Setup script" box. Safe to re-run.
set -euo pipefail

RMLUI_TAG=4.3
RMLUI_PREFIX=/usr/local

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
fi

export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update -q
$SUDO apt-get install -y -q --no-install-recommends \
  build-essential cmake git pkg-config \
  libsdl2-dev libsdl2-mixer-dev libsdl2-ttf-dev libsdl2-image-dev \
  libgl-dev libfreetype-dev libgtest-dev

if [ -e "${RMLUI_PREFIX}/lib/libRmlCore.so.${RMLUI_TAG}" ]; then
  echo "RmlUi ${RMLUI_TAG} already installed in ${RMLUI_PREFIX}"
  exit 0
fi

workdir="$(mktemp -d)"
trap 'rm -rf "${workdir}"' EXIT

git clone --depth 1 --branch "${RMLUI_TAG}" https://github.com/mikke89/RmlUi.git "${workdir}/RmlUi"
cmake -S "${workdir}/RmlUi" -B "${workdir}/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${RMLUI_PREFIX}" \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_SAMPLES=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_LUA_BINDINGS=OFF
cmake --build "${workdir}/build" -j"$(nproc)"
$SUDO cmake --install "${workdir}/build"
$SUDO ldconfig

echo "BitRiot build dependencies installed."
