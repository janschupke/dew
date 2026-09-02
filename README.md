# dew

A desktop digital synth DAW — pattern composition in the FL Studio shape: a channel
rack with a step grid, a piano roll, a playlist of pattern clips, and a mixer, driven
by a single-oscillator synth per channel.

Status: **scaffold**. Phase 1 of 6 is complete — the build, the dependency system and
the verification harness are in place and proven. The DAW itself lands in phases 2–6.

## Build

```sh
brew bundle                          # cmake >= 3.25, ninja, ccache
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Presets, not raw flags — `dev` (Debug), `release` (RelWithDebInfo), `ci` (release plus
warnings-as-errors), `offline` (release from a warm cache with no network).

Artefacts land in `build/<preset>/`:

| Target | Path |
|---|---|
| `dew` (GUI app) | `build/release/src/dew_artefacts/RelWithDebInfo/dew.app` |
| `dew_render` (offline renderer) | `build/release/tools/dew_render_artefacts/RelWithDebInfo/dew_render` |
| `dew_tests` | `build/release/tests/dew_tests` |

## Dependencies

C++ has no default package manager, so this is decided explicitly rather than by
default. Two dependencies — JUCE and Catch2 — fetched from source by
[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake).

**Versions live in exactly one place**, [`cpm-package-lock.cmake`](cpm-package-lock.cmake).
**Identity lives in one other**, [`cmake/DependencyPins.cmake`](cmake/DependencyPins.cmake),
which records the commit each tag must resolve to.

The split exists because the two goals conflict. CMake's `GIT_SHALLOW` clones with
`--depth 1 --no-single-branch`, so it can only reach branch tips — a raw commit hash is
not reliably fetchable. But a tag is mutable and a commit is not. So dew fetches shallow
**by tag** (JUCE this way is 19 MB in ~3 s instead of a 288 MB clone) and then **verifies
the resolved commit** during configure:

```
-- dew: JUCE pin verified @ e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8
```

A re-pointed upstream tag fails the build loudly instead of silently changing what gets
compiled. The check fails closed: a source tree whose identity cannot be established is
an error, never a skip.

Supporting pieces:

- **CPM is vendored**, not downloaded — [`cmake/CPM.cmake`](cmake/CPM.cmake), 45 KB,
  sha256 `1c40fc10…`, verified against upstream's published hash for v0.43.1 and
  re-checked by `check-deps.sh`. Bootstrapping needs no network and is auditable in
  `git log`.
- **`./scripts/check-deps.sh`** rejects floating refs (`master`, `HEAD`, `origin/*`),
  missing pins, non-git sources, and a modified CPM. CI runs it before the build.
- **`CPM_SOURCE_CACHE`** (`~/.cache/CPM`) means each dependency is fetched once per
  machine, not once per build directory. `cmake --preset offline` then builds with no
  network at all.
- **[`THIRD_PARTY.md`](THIRD_PARTY.md)** is generated at configure time from what was
  actually resolved — libraries, commits, licences, *and* the toolchain — so it cannot
  drift from the lock. CI fails if the committed copy is stale.

To bump a dependency: change `GIT_TAG` in the lock, resolve the new commit with
`git ls-remote <repo> 'refs/tags/<tag>*'` (take the `^{}` line for annotated tags), paste
it into `DependencyPins.cmake`, and reconfigure.

### Why not vcpkg or Conan

Both were considered. vcpkg's `juce` port is at 8.0.7, last updated May 2025, with ten
patches and a devendored Oboe — JUCE 9 would mean maintaining an overlay port. Conan
Center has no JUCE recipe at all, so it would mean authoring and hosting one. Both buy
binary caching and transitive resolution that a two-dependency project does not need.
If the graph ever grows past a handful of libraries, vcpkg manifest mode with a
`builtin-baseline` is the migration target, and the lock makes the current pins portable.

## Testing

Catch2 via CTest. `ctest --preset release` is the single command.

The GUI is tested **headlessly**: `tests/UiSmokeTests.cpp` paints components into an
offscreen `juce::Image` and asserts they produced content, so the UI layer is verified
in CI and on machines without screen-recording permission. From phase 3, `dew_render`
loads a project and renders it to WAV without touching an audio device, which is how
playback correctness gets checked without ears.

## Licence

JUCE 9 is dual-licensed: commercial, or AGPLv3. This project is a personal prototype
used under the **AGPLv3** terms. See [THIRD_PARTY.md](THIRD_PARTY.md).
