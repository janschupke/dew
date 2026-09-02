# dew

A desktop digital synth DAW — pattern composition in the FL Studio shape: a channel
rack with a step grid, a piano roll, a playlist of pattern clips, and a mixer, driven
by a single-oscillator synth per channel.

Status: **working prototype**. New, open, edit, save and playback all function end to
end, with effects and automation on top. It is not a product — but every layer is real
and wired to the next.

Start with **Demos → Getting Started** in the menu bar; there are four.

## What it does

- **Channel rack** — a step grid, one row per channel, click or drag to write steps.
  Mute and solo per channel. A pattern longer than the width scrolls rather than
  shrinking its steps into hairlines.
- **Piano roll** — scroll and zoom in time, rubber-band select, move a chord without
  losing its shape, draw velocities in the lane below, and let the pattern grow when a
  note is written past its end. A new note takes the length and velocity of the last
  one you drew. It edits *the same notes* as the step grid: a lit step is a note at the
  channel's base pitch, so there is one representation and two views.
- **Playlist** — pattern clips on tracks along a bar timeline; a clip longer than its
  pattern repeats it, as FL does. Drag clips between tracks, double-click one to open
  its pattern, and mute or solo a lane.
- **Effects** — reverb, filter, delay, drive, chorus and a 3-band EQ, chained up to
  four deep on any channel or mixer track, with bypass and reordering.
- **Automation** — clips on the playlist that drive a curated set of targets: channel
  and track volume and pan, master gain, and any parameter of any effect. Drag points
  on the curve, double-click to add one, alt-click to remove it.
- **Mixer** — a fader, pan, mute and solo per insert, plus master. Solo is resolved
  across the whole mixer, so soloing one track silences the rest.
- **Instrument** — one band-limited oscillator (sine/saw/square/triangle) with an
  octave, an ADSR envelope, and channel volume and pan.
- **Transport** — play/stop, tempo, a pattern-or-song switch with a live playhead, and
  pattern add/duplicate/delete with an editable pattern length.
- **File** — New, Open, Save, Save As, with dirty tracking and a save-before-closing
  prompt. Undo/redo covers every edit.

⌘N ⌘O ⌘S ⇧⌘S, ⌘Z ⇧⌘Z, Space to play, ⌘L to switch pattern/song, ⌘K to add a channel.

In the piano roll: ⌘-scroll to zoom, shift-scroll to scroll in time, ⌘-drag to
rubber-band, ⌘A to select every note on the channel, delete to remove the selection.

## Design system

`src/ui/design/` holds the vocabulary — semantic colour roles, a 4px spacing scale, a
type scale, and thirty-odd icons drawn as `juce::Path` rather than shipped as assets.
`src/ui/primitives/` holds the controls built on it, including `DewNumberField`, which
is the drag-up-and-down number entry used for every numeric value.

`dew_shot gallery out.png` renders every token, icon and primitive in every state onto
one page, which is both how the design system is reviewed and how it is tested.

## Architecture

Four layers, each testable without the one above it:

```
ui/       ChannelRack · PianoRoll · Playlist · Mixer · TransportBar · EffectChain
          design/ (tokens, icons) · primitives/ · TimelineView (shared step↔pixel map)
            │ edits via ProjectEdits (one undo transaction per gesture)
model/    ProjectDocument (FileBasedDocument) ── ValueTree ── ProjectSchema ── JSON
          AutomationTargets (the curated automatable set) · DemoLibrary
            │ AsyncUpdater coalesces rebuilds ─→ EngineSnapshot
engine/   SnapshotBridge → Transport → Sequencer → SynthChannel[] → EffectUnit[] → MixerBus
            │
io/       LiveAudioHost (a device)   ·   OfflineRenderer (dew_render, tests)
```

The message thread owns the ValueTree and builds snapshots. The audio thread only
reads a published snapshot: it never allocates, locks, or touches the tree.
`SnapshotBridge` is a three-slot rotation whose indices live in **one** atomic word
swapped by compare-exchange, so `front != back` is an invariant of the encoding rather
than an argument about interleavings — an earlier version reasoned its way to a design
that tore roughly twice per ten thousand publishes.

### Effects without a command queue

Effect *parameters* travel in the snapshot like everything else. Effect *instances* own
state — delay lines, reverb tanks — that has to survive a snapshot swap, and the usual
answer is a lock-free command queue with the message thread building an effect and the
audio thread swapping a pointer in.

dew does not have one. A capped pool of 32 `EffectUnit`s is allocated in `prepare()`,
each holding **all six** effect types at their maximum size, so switching a slot from
delay to reverb is a `reset()` rather than a construction. Topology then travels in the
snapshot like every other piece of project state, and the audio thread neither
allocates, frees, nor blocks — with no new concurrency primitive to get wrong. The cost
is about 11MB of mostly-idle DSP state, which is a good trade for deleting a
hand-written lock-free queue.

Pool slots are keyed on each effect's persistent id by open addressing, so the mapping
is a pure function of the ids in the document: adding an effect to an earlier channel
does not renumber the later ones and cut the reverb tail they were in the middle of.

### Automation targets are declared, not addressed

An automation clip points at `scope + targetId + slot + param`, resolved once on the
message thread to direct indices. Targets come from a table in `AutomationTargets.cpp`,
so nothing that is not a continuous quantity can be automated, and a clip pointing at a
deleted channel — or an effect slot that changed type — is dropped with a warning like
any other dangling reference.

Frequency-like parameters map exponentially rather than linearly. A cutoff swept
linearly from 20Hz to 18kHz spends four fifths of the drawn curve above 3kHz, where
almost nothing audible happens, and crosses the whole musical range in the last fifth.

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
| `dew_shot` (offscreen UI renderer) | `build/release/tools/dew_shot_artefacts/RelWithDebInfo/dew_shot` |
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

Catch2 via CTest. `ctest --preset release` runs all 147.

`dew_render` loads a project and renders it to WAV with no audio device, which is how
playback correctness is checked without ears:

```sh
dew_render examples/demo.dew out.wav              # the arrangement, once, plus its tail
dew_render examples/demo.dew out.wav --pattern 1 --seconds 8
dew_render --write-demos examples                 # regenerate the demo library
```

It exits non-zero on a silent render, because "loaded but made no sound" is the failure
it exists to catch.

Every effect has a test that asserts what it *does*, not that it ran: the lowpass
leaves 80Hz alone and removes 5kHz, the delay's peak lands within 100 samples of the
time it was set to, feedback produces repeats that decay, reverb puts sound where there
was silence, drive lowers the crest factor, and each EQ band moves the frequency it
names and not the others. Automation is checked the same way — a sweep has to produce a
measurably rising envelope through the real engine.

The GUI is tested **headlessly**: components are painted into an offscreen
`juce::Image` and asserted to have produced content, every tab is rendered, gestures are
driven through real `MouseEvent`s, and the editor's own engine is driven through
`processBlock` to prove that writing a step makes sound and that undoing it stops the
sound. That runs in CI and on machines without screen-recording permission.

`dew_shot` renders any tab, or the design-system gallery, straight to PNG:

```sh
dew_shot editor out.png --project examples/melody.dew --tab piano-roll --size 1600x1000
dew_shot gallery out.png
```

It exists because screen-recording permission is not always available, and because a
layout defect is obvious in a picture and nearly invisible in code. Both bugs reported
against the first version of this app — "channel rack clicks don't go through" and
"can't add more patterns" — turned out to be layout and affordance problems that two
rounds of code-reading had missed and one PNG made obvious.

## Licence

JUCE 9 is dual-licensed: commercial, or AGPLv3. This project is a personal prototype
used under the **AGPLv3** terms. See [THIRD_PARTY.md](THIRD_PARTY.md).
