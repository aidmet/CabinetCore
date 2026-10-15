#!/bin/sh
# Starts the CabinetCore editor once SDL2 is available.

set -u

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ -x "$here/build/cabinetcore" ]; then
    bin="$here/build/cabinetcore"
elif [ -x "$here/cabinetcore" ]; then
    bin="$here/cabinetcore"
else
    echo "CabinetCore could not find the editor." >&2
    echo "Build it with ./build.sh, then run ./cabinetcore.sh again." >&2
    exit 1
fi

install_command() {
    if command -v apt-get >/dev/null 2>&1; then
        echo "sudo apt-get install libsdl2-2.0-0"
    elif command -v dnf >/dev/null 2>&1; then
        echo "sudo dnf install SDL2"
    elif command -v yum >/dev/null 2>&1; then
        echo "sudo yum install SDL2"
    elif command -v pacman >/dev/null 2>&1; then
        echo "sudo pacman -S sdl2"
    elif command -v zypper >/dev/null 2>&1; then
        echo "sudo zypper install libSDL2-2_0-0"
    elif command -v apk >/dev/null 2>&1; then
        echo "sudo apk add sdl2"
    else
        echo "Install the package that provides libSDL2-2.0.so.0"
    fi
}

sdl_missing() {
    if command -v ldd >/dev/null 2>&1; then
        ldd "$bin" 2>&1 | grep 'not found' | grep -q 'libSDL2-2.0.so.0'
        return
    fi
    if command -v ldconfig >/dev/null 2>&1 && ldconfig -p 2>/dev/null | grep -q 'libSDL2-2.0.so.0'; then
        return 1
    fi
    for path in \
        /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 \
        /usr/lib64/libSDL2-2.0.so.0 \
        /usr/lib/libSDL2-2.0.so.0
    do
        if [ -e "$path" ]; then
            return 1
        fi
    done
    return 0
}

if sdl_missing; then
    echo "CabinetCore needs SDL2 before it can open."
    echo
    echo "Install it with this command, then run ./cabinetcore.sh again:"
    echo
    echo "    $(install_command)"
    exit 1
fi

exec "$bin" "$@"
