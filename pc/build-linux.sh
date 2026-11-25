#!/bin/bash
# Azahar VR (the PC VR build): a native Linux x86-64 build with the OpenXR frontend on.
#   pc/build-linux.sh [build dir] [-- extra cmake -D args...]
# The build dir defaults to build-pcvr in the repo. Needs the submodules checked out, Qt 6,
# SDL2, Ninja and a C++20 compiler. The options are the ones the desktop's own x86-64 builds of
# this tree use, with Sentry crash reports off (no DSN on PC yet). AZAHARFRAME_TEST_HOOKS stays on
# for now (the mock runs and the RPC tests use it); pass -DAZAHARFRAME_TEST_HOOKS=OFF to drop it.
# CONFIGURE_ONLY=1: configure and stop.
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$REPO/build-pcvr}
[ "$#" -gt 0 ] && shift
[ "${1:-}" = -- ] && shift
case "$(uname -m)" in
    x86_64) ;;
    *) echo "pc/build-linux.sh is for x86-64 (this is $(uname -m)); the Frame build is steam-frame/" >&2; exit 2 ;;
esac
cmake -S "$REPO" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_FRAME_XR=ON \
    -DAZAHARVR_PC=ON \
    -DAZAHARFRAME_SENTRY=OFF \
    -DAZAHARFRAME_RELEASE=OFF \
    -DAZAHARFRAME_TEST_HOOKS=ON \
    -DENABLE_QT=ON -DENABLE_QT_TRANSLATION=OFF -DENABLE_QT_UPDATE_CHECKER=ON \
    -DENABLE_VULKAN=ON -DENABLE_OPENGL=ON -DENABLE_SOFTWARE_RENDERER=ON \
    -DENABLE_SDL2=ON -DUSE_SYSTEM_SDL2=ON \
    -DENABLE_TESTS=ON \
    -DENABLE_FFMPEG=OFF -DENABLE_CUBEB=OFF -DENABLE_OPENAL=OFF -DENABLE_LIBUSB=OFF \
    -DENABLE_ROOM=OFF -DENABLE_WEB_SERVICE=OFF -DENABLE_DISCORD_RPC=OFF -DENABLE_SCRIPTING=OFF \
    -DENABLE_LTO=OFF -DENABLE_NATIVE_OPTIMIZATION=OFF -DCITRA_WARNINGS_AS_ERRORS=OFF \
    -DCITRA_USE_PRECOMPILED_HEADERS=OFF \
    "$@"
[ -n "${CONFIGURE_ONLY:-}" ] && exit 0
ninja -C "$BUILD" ${NINJA_JOBS:+-j"$NINJA_JOBS"} azahar tests
echo "built: $BUILD/bin/Release/azahar"
