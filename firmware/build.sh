#!/usr/bin/env bash
# Copyright (C) 2026 Mik027.
# SPDX-License-Identifier: GPL-3.0-only
# Build the CPM50D firmware for a Pico 2 / Pico 2 W.
set -euo pipefail
cd "$(dirname "$0")"

export PICO_SDK_PATH="${PICO_SDK_PATH:-$HOME/pico/pico-sdk}"
TOOLCHAIN_BIN="$HOME/pico/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin"
if [ -d "$TOOLCHAIN_BIN" ]; then
    export PATH="$TOOLCHAIN_BIN:$PATH"
fi

command -v arm-none-eabi-gcc >/dev/null || {
    echo "arm-none-eabi-gcc not found. Install the Arm GNU toolchain or set PATH."
    exit 1
}

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
echo
echo "Firmware: $(pwd)/build/cpm50d_emu.uf2"
