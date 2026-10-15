#!/bin/sh
# Opens the CabinetCore editor and saves a few views for the website.

set -eu
cd "$(dirname "$0")"

if [ ! -x build/cabinetcore ]; then
    echo "Build CabinetCore first, then run sh screenshots.sh" >&2
    exit 1
fi

mkdir -p screenshots
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    SDL_VIDEODRIVER=dummy
    export SDL_VIDEODRIVER
fi

exec ./build/cabinetcore --screenshots screenshots
