#!/usr/bin/env bash
# ==============================================================================
# Guards the determinism of the dependency set. Run by CI and by hand:
#
#     ./scripts/check-deps.sh
#
# Fails if any package in cpm-package-lock.cmake:
#   - is fetched from a floating ref (a branch rather than a tag), or
#   - has no 40-hex DEW_PIN_<name> in cmake/DependencyPins.cmake, or
#   - is declared without a git repository at all.
#
# CMake verifies the pins at configure time; this script catches a lock that was
# edited into an unpinnable state before anyone spends minutes on a build.
# ==============================================================================
set -euo pipefail

cd "$(dirname "$0")/.."

LOCK=cpm-package-lock.cmake
PINS=cmake/DependencyPins.cmake
CPM=cmake/CPM.cmake
CPM_SHA256=1c40fc102ce9625d7de7eb14f541cab30cc3138dca627f0b0ec40293ce6c2934

fail=0
note()  { printf '  %s\n' "$*"; }
error() { printf 'FAIL  %s\n' "$*"; fail=1; }
ok()    { printf 'ok    %s\n' "$*"; }

for f in "$LOCK" "$PINS" "$CPM"; do
  [[ -f $f ]] || { error "missing $f"; exit 1; }
done

# --- the vendored bootstrap must be the audited file -------------------------
actual_cpm=$(shasum -a 256 "$CPM" | awk '{print $1}')
if [[ $actual_cpm == "$CPM_SHA256" ]]; then
  ok "vendored CPM.cmake matches the audited sha256"
else
  error "vendored CPM.cmake has been modified"
  note "expected $CPM_SHA256"
  note "actual   $actual_cpm"
fi

# --- every declared package must be pinned -----------------------------------
packages=$(grep -oE '^CPMDeclarePackage\(([A-Za-z0-9_]+)' "$LOCK" | sed 's/^CPMDeclarePackage(//')

if [[ -z $packages ]]; then
  error "no packages declared in $LOCK"
fi

for pkg in $packages; do
  block=$(awk -v p="CPMDeclarePackage($pkg" 'index($0,p)==1{f=1} f{print} f&&/^\)/{exit}' "$LOCK")

  if ! grep -qE 'GIT_REPOSITORY[[:space:]]+\S+' <<<"$block"; then
    error "$pkg: no GIT_REPOSITORY - only git-sourced packages can be commit-pinned"
    continue
  fi

  tag=$(grep -oE 'GIT_TAG[[:space:]]+\S+' <<<"$block" | awk '{print $2}')
  if [[ -z $tag ]]; then
    error "$pkg: no GIT_TAG"
    continue
  fi

  case $tag in
    master|main|HEAD|develop|trunk|origin/*|refs/heads/*)
      error "$pkg: GIT_TAG '$tag' is a floating ref - use a release tag"
      continue ;;
  esac

  pin=$(grep -oE "^set\(DEW_PIN_${pkg}[[:space:]]+\"[0-9a-f]{40}\"" "$PINS" | grep -oE '[0-9a-f]{40}' || true)
  if [[ -z $pin ]]; then
    error "$pkg: no 40-hex DEW_PIN_${pkg} in $PINS"
    note  "resolve it with: git ls-remote <repo> 'refs/tags/${tag}*'"
    continue
  fi

  ok "$pkg pinned at $tag -> ${pin:0:12}"
done

echo
if (( fail )); then
  echo "dependency check FAILED"
  exit 1
fi
echo "dependency check passed"
