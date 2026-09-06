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

step "generated cursor rules"
./scripts/gen-cursor-rules.sh --check

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

# CMake files are OUTSIDE the sweep above, and that is how they came to be
# damaged: clang-format was run over src/CMakeLists.txt once, read every `#` as
# a preprocessor directive and reformatted the prose after it as C++. Seventy
# four comment lines became "#dew_model the project : ValueTree, ... .A leaf.",
# the banner rules became "#== == ==", and nothing in the tree could see it -
# so the file's next author wrote MORE comments in the broken style.
#
# dew's CMake comment is always "# ", so the damage has a one-character
# signature and the check is a grep. CPM.cmake is upstream's file, pinned by
# sha256 in check-deps.sh, and dew does not get to judge its style.
cmake_count=0
cmake_bad=""

while IFS= read -r f; do
  cmake_count=$((cmake_count + 1))
  hits=$(grep -nH '^#[A-Za-z]' "$f" || true)
  [[ -n $hits ]] && cmake_bad="${cmake_bad}${hits}"$'\n'
done < <(git ls-files '*.cmake' 'CMakeLists.txt' '*/CMakeLists.txt' \
         | grep -Ev '^(cmake/CPM\.cmake|cpm-package-lock\.cmake)$')

if [[ -n $cmake_bad ]]; then
  echo "FAIL  a CMake comment is missing the space after '#' - clang-format has"
  echo "      probably been run over it, which also mangles the prose:"
  printf '%s' "$cmake_bad" | sed 's/^/      /'
  exit 1
fi
echo "ok    $cmake_count cmake files carry '# ' comments"

step "website"
# Before the C++ build: it fails cheapest first, and it needs nothing the build
# produces. That the JSON it reads is CURRENT is a separate question, answered
# by a test in the suite below.
./scripts/check-website.sh "$@"

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
