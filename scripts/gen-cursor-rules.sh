#!/usr/bin/env bash
# ==============================================================================
# .cursor/rules/main.mdc, from AGENTS.md.
#
#     ./scripts/gen-cursor-rules.sh              write it
#     ./scripts/gen-cursor-rules.sh --check      fail if it is stale
#
# Cursor reads one file and Claude reads another, and a repo that maintains both
# by hand maintains one of them: the two drift, and the wrong copy is
# indistinguishable from the right one. So AGENTS.md is written and this is
# derived - frontmatter Cursor needs, then the same text with its links made
# relative to a file two directories down.
#
# --check runs in ./scripts/check.sh, before anything is built. It is not a CI
# job: CI has no reason to care which editor a change was written in, and a
# stale file is a local inconvenience rather than a broken build.
# ==============================================================================
set -euo pipefail

cd "$(dirname "$0")/.."

source=AGENTS.md
target=.cursor/rules/main.mdc

check=0
[[ ${1:-} == --check ]] && check=1

[[ -f $source ]] || { echo "FAIL  $source is missing"; exit 1; }

# A link in AGENTS.md is written from the repo root; main.mdc sits two levels
# down. Only root-relative markdown targets are rewritten - an anchor (#...), an
# absolute URL and a bare word are all left alone.
render() {
  printf '%s\n' \
    '---' \
    'description: dew — all assistant instructions' \
    'alwaysApply: true' \
    '---' \
    '' \
    '<!-- Generated from AGENTS.md by scripts/gen-cursor-rules.sh. Do not edit. -->'
  sed -E 's#\]\((\.ai/|README\.md|src/|tests/|tools/|scripts/|cmake/|examples/|presets/|CMakePresets\.json|THIRD_PARTY\.md)#](../../\1#g' "$source"
}

if (( check )); then
  if [[ ! -f $target ]]; then
    echo "FAIL  $target is missing - run ./scripts/gen-cursor-rules.sh"
    exit 1
  fi

  scratch=$(mktemp)
  trap 'rm -f "$scratch"' EXIT
  render > "$scratch"

  if ! diff -u "$target" "$scratch"; then
    echo
    echo "FAIL  $target does not match $source - run ./scripts/gen-cursor-rules.sh"
    exit 1
  fi

  echo "ok    $target matches $source"
  exit 0
fi

mkdir -p "$(dirname "$target")"
render > "$target"
echo "wrote $target from $source"
