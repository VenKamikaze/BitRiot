#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Install a pinned Emscripten SDK, for BitRiot's WebAssembly builds (the determinism gate, scripts/determinism-gate.sh,
# and the WASM port) and for vnet's browser build.
#
#   scripts/setup-emsdk.sh                  # installs into ./.emsdk (git-ignored)
#   EMSDK_DIR=/opt/emsdk scripts/setup-emsdk.sh
#   source "$EMSDK_DIR/emsdk_env.sh"        # then: emcc --version
#
# Both the emsdk repository revision and the Emscripten version are pinned, so
# every machine and CI run gets the same compiler. emsdk is fetched with git
# (never a GitHub archive download, which some networks block); the toolchain
# binaries come from storage.googleapis.com. The gate needs no Emscripten ports
# (bitriot_sim has no SDL), so nothing else is downloaded.
set -euo pipefail

EMSDK_COMMIT="d7b4130688bfa348392b921ab04015aa15c4f154"  # emscripten-core/emsdk, 2026-10-20
EMSDK_VERSION="6.0.13"                                    # Emscripten release installed by that revision

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EMSDK_DIR="${EMSDK_DIR:-$root/.emsdk}"

if [ ! -d "$EMSDK_DIR/.git" ]; then
    git init -q "$EMSDK_DIR"
    git -C "$EMSDK_DIR" remote add origin https://github.com/emscripten-core/emsdk.git
fi
if [ "$(git -C "$EMSDK_DIR" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$EMSDK_COMMIT" ]; then
    git -C "$EMSDK_DIR" fetch -q --depth 1 origin "$EMSDK_COMMIT"
    git -C "$EMSDK_DIR" checkout -q --force "$EMSDK_COMMIT"
fi

cd "$EMSDK_DIR"
./emsdk install "$EMSDK_VERSION"
./emsdk activate "$EMSDK_VERSION"

echo
echo "emsdk $EMSDK_VERSION is installed in $EMSDK_DIR"
echo "Use it with:  source \"$EMSDK_DIR/emsdk_env.sh\""
