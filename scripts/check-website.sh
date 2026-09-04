#!/usr/bin/env bash
# ==============================================================================
# The website's half of the gate.
#
#     ./scripts/check-website.sh              types, lint, formatting, tests, build
#     ./scripts/check-website.sh --no-build   everything but `next build`
#
# Split out of check.sh for the reason check-deps.sh is: check.sh is a list of
# steps, and a step fifteen lines long has stopped being one.
#
# Nothing else in this repo looks at website/. The formatting step reads
# `find src tests tools`, and every source gate walks DEW_SOURCE_DIR - so the
# site has no gate but this one, which is why it has several of its own.
#
# SKIPPED, LOUDLY, when there is no node. dew is a C++ project and somebody who
# never opens the site should not be stopped by a missing runtime. But check.sh
# already argues that "a gate that quietly does nothing is worse than one that
# is not there", so this names the check that did not run and how to make it.
# CI sets DEW_REQUIRE_NODE=1, where a silent skip would be invisible.
#
# What the JSON the site reads is CURRENT is not checked here: that is a C++
# question, answered by dew_tests where the emitters are.
# ==============================================================================
set -euo pipefail

cd "$(dirname "$0")/.."

build=1
[[ ${1:-} == --no-build ]] && build=0

if ! command -v node >/dev/null 2>&1; then
  if [[ ${DEW_REQUIRE_NODE:-0} == 1 ]]; then
    echo "FAIL  node not found and DEW_REQUIRE_NODE=1"
    exit 1
  fi

  echo "skip  node not found - the website is NOT checked"
  echo "      install it with: brew bundle   (see website/.nvmrc for the version)"
  exit 0
fi

# Reported, not enforced. A mismatch is worth knowing about; refusing to run
# over it would be the gate deciding which node somebody may have, and CI pins
# the version that actually counts.
wanted=$(tr -d 'v \n' < website/.nvmrc)
actual=$(node --version | tr -d 'v\n')

if [[ ${actual%%.*} != "${wanted%%.*}" ]]; then
  echo "warn  node ${actual}; website/.nvmrc asks for ${wanted}, which is what CI uses"
fi

cd website

# ci, not install: it installs exactly what package-lock.json says, refuses a
# lock that disagrees with package.json, and never REWRITES the lock. That last
# one matters - a gate that dirties the tree it is judging is one nobody can
# run on a tree with work in it.
#
# Re-run when the lock is newer than the tree it produced, so a dependency bump
# is not silently checked against yesterday's node_modules.
if [[ ! -d node_modules || package-lock.json -nt node_modules ]]; then
  npm ci
fi

npm run theme:check
npm run format:check
npm run lint
npm run typecheck
npm run test

if (( build )); then
  npm run build
fi

echo "ok    website theme, formatting, lint, types and tests pass"
