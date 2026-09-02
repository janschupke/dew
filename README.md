# dew

A desktop digital synth DAW — pattern composition in the FL Studio shape: a channel
rack with a step grid, a piano roll, a playlist of pattern clips, and a mixer, driven
by a single-oscillator synth per channel.

Status: **working prototype**. New, open, edit, save and playback all function end to
end. It is a skeleton, not a product — but every layer is real and wired to the next.

## What it does

- **Channel rack** — a step grid, one row per channel, click or drag to write steps.
- **Piano roll** — click to add, drag to move, drag the right edge to resize,
  right-click to delete. It edits *the same notes* as the step grid: a lit step is a
  note at the channel's base pitch, so there is one representation and two views.
- **Playlist** — pattern clips on tracks along a bar timeline; a clip longer than its
  pattern repeats it, as FL does.
- **Mixer** — a fader, pan, mute and solo per insert, plus master. Solo is resolved
  across the whole mixer, so soloing one track silences the rest.
- **Instrument** — one band-limited oscillator (sine/saw/square/triangle) with an
  octave, an ADSR envelope, and channel volume and pan.
- **Transport** — play/stop, tempo, and a pattern-or-song switch with a live playhead.
- **File** — New, Open, Save, Save As, with dirty tracking and a save-before-closing
  prompt. Undo/redo covers every edit.

⌘N ⌘O ⌘S ⇧⌘S, ⌘Z ⇧⌘Z, Space to play, ⌘L to switch pattern/song, ⌘K to add a channel.

## Architecture

Four layers, each testable without the one above it:

```
ui/       ChannelRack · PianoRoll · Playlist · Mixer · TransportBar · InstrumentPanel
            │ edits via ProjectEdits (one undo transaction per gesture)
model/    ProjectDocument (FileBasedDocument) ── ValueTree ── ProjectSchema ── JSON
            │ AsyncUpdater coalesces rebuilds ─→ EngineSnapshot
engine/   SnapshotBridge → Transport → Sequencer → SynthChannel[] → MixerBus
            │
io/       LiveAudioHost (a device)   ·   OfflineRenderer (dew_render, tests)
```

The message thread owns the ValueTree and builds snapshots. The audio thread only
reads a published snapshot: it never allocates, locks, or touches the tree.
`SnapshotBridge` is a three-slot rotation whose indices live in **one** atomic word
swapped by compare-exchange, so `front != back` is an invariant of the encoding rather
than an argument about interleavings — an earlier version reasoned its way to a design
that tore roughly twice per ten thousand publishes.

The engine knows nothing about audio devices. It is prepared with a sample rate and a
block size and fills a buffer, so live playback and offline rendering run the same code
and a passing render says something about the real engine.

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

## The project file

One JSON file, `formatVersion`-stamped. The schema is declared once in
`src/model/ProjectSchema.cpp` as a table of node specs with per-property defaults, and
that table drives reading, writing and validation — so there is no hand-written writer
to drift out of step with a hand-written parser.

- A property absent from the file takes its default, so older files load.
- A property of the wrong type takes its default and warns, rather than failing.
- A key the schema does not know is dropped and reported.
- A newer `formatVersion` is refused outright instead of half-read.

Saving writes to a temporary and swaps, so an interrupted save cannot destroy the
project it was overwriting.

## Testing

Catch2 via CTest. `ctest --preset release` runs all 54.

`dew_render` loads a project and renders it to WAV with no audio device, which is how
playback correctness is checked without ears:

```sh
dew_render examples/demo.dew out.wav              # the arrangement, once, plus its tail
dew_render examples/demo.dew out.wav --pattern 1 --seconds 8
dew_render --write-demo examples/demo.dew         # regenerate the example
```

It exits non-zero on a silent render, because "loaded but made no sound" is the failure
it exists to catch.

The GUI is tested **headlessly**: components are painted into an offscreen
`juce::Image` and asserted to have produced content, every tab is rendered, and the
editor's own engine is driven through `processBlock` to prove that writing a step makes
sound and that undoing it stops the sound. That runs in CI and on machines without
screen-recording permission.

## Licence

JUCE 9 is dual-licensed: commercial, or AGPLv3. This project is a personal prototype
used under the **AGPLv3** terms. See [THIRD_PARTY.md](THIRD_PARTY.md).
