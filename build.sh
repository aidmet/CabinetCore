#!/bin/sh
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
echo "Editor:  ./cabinetcore.sh"
echo "Runtime: ./build/cabinet.elf"
