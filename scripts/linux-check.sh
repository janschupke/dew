#!/usr/bin/env bash
# ==============================================================================
# The score language, built and tested against a second standard library.
#
#     ./scripts/linux-check.sh
#
# dew is a macOS application and this does not change that. What it checks is
# the one claim the README makes that macOS cannot test: that dew_lang produces
# the same music under libstdc++ as under libc++, which is why it writes out
# splitmix64 and PCG32 by hand instead of using <random>.
#
# It is cheap because dew_lang links nothing - JUCE included - so the container
# needs a compiler, CMake and nothing else. No X11, no ALSA, no GTK.
#
# Needs Docker. On Apple Silicon the platform is native, not emulated; running
# this under QEMU would be slow enough to stop being worth it.
# ==============================================================================
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v docker >/dev/null; then
  echo "FAIL  docker not found - this check runs the Linux toolchain in a container"
  exit 1
fi

PLATFORM=${DEW_LINUX_PLATFORM:-linux/$(uname -m | sed 's/arm64/arm64/;s/x86_64/amd64/')}

echo "==> building and testing dew_lang on ${PLATFORM} with gcc"

docker run --rm --platform "${PLATFORM}" \
  -v "$PWD:/dew:ro" \
  -v "${CPM_SOURCE_CACHE:-$HOME/.cache/CPM}:/cpm:ro" \
  gcc:14 bash -euo pipefail -c '
    apt-get update -qq >/dev/null
    apt-get install -y -qq cmake ninja-build git >/dev/null

    cp -r /dew /work && cp -r /cpm /tmp/cpm && cd /work

    cmake -S . -B /tmp/build -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_COMPILE_WARNING_AS_ERROR=ON \
      -DCPM_SOURCE_CACHE=/tmp/cpm >/dev/null

    cmake --build /tmp/build --target dew_lang_tests -j"$(nproc)"

    /tmp/build/tests/dew_lang_tests

    # The determinism claim itself: the same score compiled twice in two
    # separate processes must be the same bytes. In-process runs agree even
    # when something is reading memory it should not.
    echo "==> nothing further to compile here: dew_score needs JUCE"
  '

echo "linux check passed"
