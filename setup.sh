#!/bin/sh
# Builds CabinetCore, then starts the editor through the launcher.

set -eu
cd "$(dirname "$0")"

need_sdl=0
if ! ldconfig -p 2>/dev/null | grep -q 'libSDL2-2.0.so.0' \
    && [ ! -e /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 ] \
    && [ ! -e /usr/lib64/libSDL2-2.0.so.0 ] \
    && [ ! -e /usr/lib/libSDL2-2.0.so.0 ]; then
    need_sdl=1
fi

need_cmake=0
command -v cmake >/dev/null 2>&1 || need_cmake=1

need_cxx=0
if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    need_cxx=1
fi

install_command() {
    if command -v apt-get >/dev/null 2>&1; then
        packages=""
        [ "$need_cmake" -eq 1 ] && packages="$packages cmake"
        [ "$need_cxx" -eq 1 ] && packages="$packages g++"
        [ "$need_sdl" -eq 1 ] && packages="$packages libsdl2-2.0-0"
        echo "sudo apt-get install$packages"
    elif command -v dnf >/dev/null 2>&1; then
        packages=""
        [ "$need_cmake" -eq 1 ] && packages="$packages cmake"
        [ "$need_cxx" -eq 1 ] && packages="$packages gcc-c++"
        [ "$need_sdl" -eq 1 ] && packages="$packages SDL2"
        echo "sudo dnf install$packages"
    elif command -v yum >/dev/null 2>&1; then
        packages=""
        [ "$need_cmake" -eq 1 ] && packages="$packages cmake"
        [ "$need_cxx" -eq 1 ] && packages="$packages gcc-c++"
        [ "$need_sdl" -eq 1 ] && packages="$packages SDL2"
        echo "sudo yum install$packages"
    elif command -v pacman >/dev/null 2>&1; then
        packages=""
        [ "$need_cmake" -eq 1 ] && packages="$packages cmake"
        [ "$need_cxx" -eq 1 ] && packages="$packages gcc"
        [ "$need_sdl" -eq 1 ] && packages="$packages sdl2"
        echo "sudo pacman -S$packages"
    elif command -v zypper >/dev/null 2>&1; then
        packages=""
        [ "$need_cmake" -eq 1 ] && packages="$packages cmake"
        [ "$need_cxx" -eq 1 ] && packages="$packages gcc-c++"
        [ "$need_sdl" -eq 1 ] && packages="$packages libSDL2-2_0-0"
        echo "sudo zypper install$packages"
    elif command -v apk >/dev/null 2>&1; then
        packages=""
        [ "$need_cmake" -eq 1 ] && packages="$packages cmake"
        [ "$need_cxx" -eq 1 ] && packages="$packages g++"
        [ "$need_sdl" -eq 1 ] && packages="$packages sdl2"
        echo "sudo apk add$packages"
    else
        echo "Install CMake, a C++ compiler, and libSDL2-2.0.so.0"
    fi
}

if [ "$need_sdl" -eq 1 ] || [ "$need_cmake" -eq 1 ] || [ "$need_cxx" -eq 1 ]; then
    echo "CabinetCore needs a few packages before it can build."
    echo
    echo "Install them with this command, then run sh setup.sh again:"
    echo
    echo "    $(install_command)"
    exit 1
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
exec ./cabinetcore.sh "$@"
