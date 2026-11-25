#!/usr/bin/env bash
# Builds device/files/libC2paDps-shim.so (arm64, bionic) with the Android NDK.
#   NDK=~/Library/Android/sdk/ndk/<ver> src/c2pashim/build.sh
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
NDK=${NDK:-$( (ls -d "$HOME"/Library/Android/sdk/ndk/* "$HOME"/Android/Sdk/ndk/* 2>/dev/null || true) | sort -V | tail -n 1)}
CC=$(ls "$NDK"/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android33-clang | head -n 1)
"$CC" -O2 -shared -fPIC -s -Wall -Wextra -Wno-unused-parameter -Wl,-soname,libC2paDps.camera.samsung.so \
  -o "$D/../../device/files/libC2paDps-shim.so" "$D/c2pashim.c" -llog -ldl
echo "Built device/files/libC2paDps-shim.so"
