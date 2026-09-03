#!/usr/bin/env bash
# ==============================================================================
# The gate, as one command.
#
#     ./scripts/check.sh
#
# It was four commands in a README code block, which is the shape of thing that
# gets run three-quarters of the time. Everything CI runs, in the order that
# fails cheapest first: the dependency pins and the formatting need no build.
#
#   --no-build   skip the compile, for when the tree is already built
# ==============================================================================
set -euo pipefail

cd "$(dirname "$0")/.."

build=1
[[ ${1:-} == --no-build ]] && build=0

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }

step "dependency pins"
./scripts/check-deps.sh

step "formatting"
# Xcode ships clang-format, so this needs nothing installed. A version that
# cannot be found is reported rather than skipped silently: a gate that quietly
# does nothing is worse than one that is not there.
if ! CLANG_FORMAT=$(xcrun -f clang-format 2>/dev/null) && ! CLANG_FORMAT=$(command -v clang-format); then
  echo "FAIL  clang-format not found (Xcode command line tools provide it)"
  exit 1
fi

# No mapfile: macOS ships bash 3.2, and this script has to run where it is run.
count=$(find src tests tools \( -name '*.cpp' -o -name '*.h' \) | wc -l | tr -d ' ')
find src tests tools \( -name '*.cpp' -o -name '*.h' \) -print0 \
  | xargs -0 "$CLANG_FORMAT" --dry-run -Werror
echo "ok    $count files formatted as .clang-format says"

if (( build )); then
  step "build (warnings are errors)"
  cmake --build --preset ci
fi

step "tests"
# Parallel: 110 seconds of serial test time becomes about 40, and the suite is
# built to take it - nothing binds a fixed port or writes a shared path.
ctest --preset ci --parallel 4

step "generated manifest is current"
git diff --exit-code -- THIRD_PARTY.md
echo "ok    THIRD_PARTY.md matches what configure wrote"

printf '\n\033[1;32mgate passed\033[0m\n'
