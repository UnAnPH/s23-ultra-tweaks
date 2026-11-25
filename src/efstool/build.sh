#!/usr/bin/env bash
# Builds device/files/efstool (static arm64 for the phone) with the Android NDK.
#   NDK=~/Library/Android/sdk/ndk/<ver> src/efstool/build.sh
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
NDK=${NDK:-$( (ls -d "$HOME"/Library/Android/sdk/ndk/* "$HOME"/Android/Sdk/ndk/* 2>/dev/null || true) | sort -V | tail -n 1)}
CC=$(ls "$NDK"/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android34-clang | head -n 1)
"$CC" -O2 -static -s -Wall -Wextra -o "$D/../../device/files/efstool" "$D/efstool.c"
echo "Built device/files/efstool"
