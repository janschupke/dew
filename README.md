# dew

A desktop digital synth DAW — pattern composition in the FL Studio shape: a channel
rack with a step grid, a piano roll, a playlist of pattern clips, and a mixer, driven
by a three-oscillator synth per channel.

Status: **working prototype**. New, open, edit, save and playback all function end to
end, with effects and automation on top. It is not a product — but every layer is real
and wired to the next.

Start with **Demos → Getting Started** in the menu bar; there are four.

## What it does

- **Channel rack** — a step grid, one row per channel, click or drag to write steps.
  Mute and solo per channel. A pattern longer than the width scrolls rather than
  shrinking its steps into hairlines. Right-click a row to rename, add or remove that
  channel; **+ Channel** sits under the last one, where the next will appear.
- **Piano roll** — scroll and zoom in time (wheel, ⌘-wheel or a trackpad pinch),
  rubber-band select, move a chord without losing its shape, grab and drag velocity bars
  in the lane below, click the keys to hear them, and let the pattern grow when a note is
  written past its end. A new note takes the length and velocity of the last
  one you drew. It edits *the same notes* as the step grid: a lit step is a note at the
  channel's base pitch, so there is one representation and two views.
- **Piano roll tools** — a strip above the ruler: select, paint and slice, a snap grid
  from 1/16 to a bar, quantize, transpose by a semitone or an octave, and a randomize
  dialog for velocity and timing. Paint writes a note per grid cell a stroke crosses;
  slice cuts every note a dragged line passes through. Everything except the tools acts
  on **the selection if there is one, and the whole channel otherwise** — so a pattern can
  be humanised or tightened without selecting anything first.
- **Playlist** — pattern clips on tracks along a bar timeline; a clip longer than its
  pattern repeats it, as FL does. Drag clips between tracks, double-click one to open
  its pattern, and mute or solo a lane. Right-click a clip for its pattern or to delete
  it, empty space to place one, and a track header to rename, add or remove that track;
  **+ Track** sits under the last one. Alt-click still deletes a clip outright.
- **Effects** — reverb, filter, delay, drive, chorus and a 3-band EQ, chained up to four
  deep on any channel, mixer track or the master. One editor, pointed either way round:
  down the instrument panel it is an accordion, each effect a card that expands in place
  with several open at once; across the bottom of the mixer it is a single row of cards,
  all open, scrolling sideways. Drag a card by its grip to reorder it.
- **Automation** — clips on the playlist that drive a curated set of targets: channel
  and track volume and pan, master gain, and any parameter of any effect. Drag points
  on the curve, double-click to add one, alt-click to remove it.
- **Mixer** — two rows. Along the top, a fader, pan, mute, solo and a peak meter per
  insert, plus master; along the bottom, the effect chain of whichever strip is selected.
  Each strip lists the channels routed into it, and clicking one goes to that channel.
  Solo is resolved across the whole mixer, so soloing one track silences the rest.
- **Instrument** — three band-limited oscillators (sine/saw/square/triangle), each
  with its own octave, detune and gain and its own on/off switch, edited a tab at a
  time. Only the first is on by default, so a new channel is a single oscillator until
  you stack one on it. Behind them, one ADSR envelope, and channel volume and pan.
- **Loop** — a span selected on any ruler is the span that plays: the transport wraps
  inside it rather than around the whole pattern or the whole song, so eight bars can be
  worked on without hearing the other fifty-six. A loop drawn ahead of the playhead is
  played into rather than jumped to; one drawn behind it snaps to its start.
- **Transport** — play/stop, tempo, a pattern-or-song switch, and pattern
  add/duplicate/delete with an editable pattern length. Click or drag any ruler — piano
  roll, sequencer or playlist — to move the position. The indicator stays on screen while
  stopped, dimmed, so Stop visibly returns it to the start rather than hiding it.
- **Visualisation** — an oscilloscope and a spectrum of the master output at the right
  end of the transport bar. Empty when nothing is sounding — the wells and their
  baselines stay put, so nothing moves when sound starts — and filled the moment signal
  flows, including a piano key clicked with the transport stopped. The trace is
  triggered on a rising zero crossing, so it stands still instead of shimmering. Hidden
  below about a thousand pixels of window, because a sixty-pixel oscilloscope is not a
  smaller oscilloscope.
- **Status bar** — what the editors are pointed at, transient messages that expire
  instead of standing forever, and the DSP load and dropout count.
- **Audio settings** — driver, output, input, sample rate and buffer size, with the
  resulting latency in milliseconds and a test tone. Under **Audio**, or ⌘,.
- **It remembers** — window geometry, the active tab, selections, the piano roll's zoom,
  scroll and snap grid, the panel width and the chosen device all come back next launch.
  The piano roll's *tool* deliberately does not: restoring into paint or slice would mean
  the first click of a session writes or cuts something nobody asked for.
- **Render** — the song, one pattern, or a span of bars selected on the playlist ruler,
  written as WAV (16/24-bit or 32-bit float), FLAC, MP3 or MIDI. Sample rate, a release
  tail, normalize, fades, dither on 16-bit, and stems - one file per mixer track. It runs
  off the message thread, so the window stays alive. Under **File**, or ⌘E.
- **File** — New, Open, Save, Save As, with dirty tracking and a save-before-closing
  prompt. Undo/redo covers every edit.

⌘N ⌘O ⌘S ⇧⌘S, ⌘E to render, ⌘Z ⇧⌘Z, Space to play, ⌘L to switch pattern/song, ⌘K to add
a channel.

On any ruler: drag to scrub, shift-drag to select a span, ⌘-click to span from the
playhead to where you clicked, and shift-click or double-click to drop the selection.
The playlist selects bars of the song and the piano roll steps of a pattern; either way
**the span is what plays** — the transport loops inside it until you clear it.

In the piano roll: ⌘-scroll or pinch to zoom, shift-scroll to scroll in time, ⌘-drag to
rubber-band, ⌘A to select every note on the channel, delete to remove the selection,
right-click empty space to deselect.
1 2 3 pick select, paint and slice; ↑ ↓ transpose a semitone and ⇧↑ ⇧↓ an octave; Q
quantizes and R opens randomize. Holding shift suspends the snap grid for the length of a
drag, which is the only way to reach an off-grid position without changing the dropdown.

Right-drag erases, and means the same thing in the piano roll and the step sequencer: one
undo step for the whole sweep, and it fills the cells between drag samples, so a quick
flick does not leave survivors behind it. Alt-drag is the same gesture. A right-press that
lets go having erased nothing was never an erase, so in the piano roll it clears the
selection instead — the sweep still starts on empty space, which is how you sweep *into*
notes.

### One bug behind three complaints

`juce::Component` intercepts mouse clicks by default, and `Label::setEditable` does not
change that — it only touches keyboard focus. So a child widget silently ate the press
and the row's own `mouseDown` never ran. A channel header's name label covered its whole
left half; a mixer strip's fader took all its remaining height, leaving selection
reachable only through a 6px border. Three separately reported "this doesn't do
anything" problems, one cause.

Display-only labels no longer intercept, and renaming moved to a double-click. Controls
that must keep their click — a fader cannot give one away and still be draggable — select
their row through their own callback instead. The tests count points in a row that land
on a click-swallowing child: 168 on a header and 68 on a strip before, zero after.

## Design system

`src/ui/design/` holds the vocabulary — semantic colour roles, a 4px spacing scale, a
type scale, and thirty-odd icons drawn as `juce::Path` rather than shipped as assets.
`src/ui/primitives/` holds the controls built on it, including `DewNumberField`, which
is the drag-up-and-down number entry used for every numeric value.

`dew_shot gallery out.png` renders every token, icon and primitive in every state onto
one page, which is both how the design system is reviewed and how it is tested.

The type scale is five sizes, and it is enforced rather than merely documented. Twelve
distinct sizes were reaching the screen: six font paths fell through to JUCE's defaults
and landed on values that exist nowhere in the system — 10.8 on an 18px button, 17 in a
menu, 13 **bold** on every tooltip — and the label a `Slider` builds for its own text box
kept JUCE's 15pt default inside a 15px box. Those hooks are overridden now, and a test
scans every source file and fails on a raw `FontOptions` outside `Tokens.cpp`, naming the
file and line.

## Architecture

Four layers, each testable without the one above it:

```
app/      Settings (window, view and device state, validated on read)
ui/       ChannelRack · PianoRoll · Playlist · Mixer · TransportBar
          EffectChainHost (heading, add button, scrolling) → EffectChainComponent
          StatusBar · AudioSettingsPanel · MidiSettingsPanel · PianoRollToolbar · RandomizePanel
          design/ (tokens, icons) · primitives/ · TimelineView (shared step↔pixel map)
          TimelineRuler (one ruler, drawn and clicked the same way in three editors)
            │ edits via ProjectEdits (one undo transaction per gesture)
model/    ProjectDocument (FileBasedDocument) ── ValueTree ── ProjectSchema ── JSON
          AutomationTargets (the curated automatable set) · DemoLibrary
          NoteTools (snap, quantize, transpose, slice, randomize - no GUI)
            │ AsyncUpdater coalesces rebuilds ─→ EngineSnapshot
            │ PreviewQueue carries auditioned notes ─────────┐
engine/   SnapshotBridge → Transport → Sequencer → SynthChannel[] → EffectUnit[] → MixerBus
            │
io/       LiveAudioHost (a device)   ·   MidiInputHost + MidiRouter (a controller)
          OfflineRenderer (dew_render, tests)
```

Clicking a piano key has to make a sound without the sequencer running, so preview notes
reach the audio thread through `PreviewQueue` — a single-producer ring, not a
latest-wins atomic, because both halves of a fast click can land inside one 5.8ms block
and latest-wins would let the release overwrite the press and the key would be silent.

### MIDI input: two rings, and controllers that are not queued at all

`PreviewQueue` is single-producer *by construction* — the writer owns `writeIndex`, the reader
owns `readIndex` — and its producer is the message thread. MIDI callbacks arrive on the OS MIDI
thread, so sharing one queue would put two producers on one index. dew gives MIDI its **own**
`PreviewQueue` instead: the invariant holds exactly as written for each ring, the audio thread
drains both, and no new concurrency primitive appears. Marshalling MIDI to the message thread
would have kept one producer at the cost of message-loop latency between a key and its note,
which is the one thing a controller exists to avoid.

Pitch bend and the mod wheel do **not** go through a ring. A ring earns its place for notes
because both halves of a click matter — press and release can land inside one block, and
latest-wins would lose the press. A wheel position has no halves: only the newest value is
audible, and a sweep sends hundreds of messages a second, which is exactly the traffic that would
fill a 64-entry ring and start refusing notes. So controllers are per-channel latest-wins
atomics, read once per block, in the same idiom as the meters.

`MidiRouter` holds every rule (velocity-0 is a note-off, channel filter, sustain, transpose, bend
scaling) and takes a `juce::MidiMessage` rather than a device, so all of it is testable without
hardware. `MidiInputHost` owns only which ports exist — and one correction to JUCE: a device
enabled during a session that is unplugged leaves `AudioDeviceManager` holding a dead port it
believes is still open, so it never reopens on replug. The host reconciles on every device-list
change, and `MidiInputHost::actionFor` pins that rule as a truth table.

The message thread owns the ValueTree and builds snapshots. The audio thread only
reads a published snapshot: it never allocates, locks, or touches the tree.
`SnapshotBridge` is a three-slot rotation whose indices live in **one** atomic word
swapped by compare-exchange, so `front != back` is an invariant of the encoding rather
than an argument about interleavings — an earlier version reasoned its way to a design
that tore roughly twice per ten thousand publishes.

### The one path that runs the other way

`SignalTap` carries the finished master output back to the display, and it is the only
thing in dew that goes audio → message. It wants the opposite contract from the two
above: `PreviewQueue` delivers every event because both halves of a click matter, and
`SnapshotBridge` hands over whole states because half a state is nonsense, but a
visualiser wants only the newest two thousand samples and every sample older than that is
worthless. Delivering all of them in order would mean a display stalled behind an open
menu comes back and redraws a third of a second of the past.

So it is a ring that overwrites its oldest samples without asking, plus one monotonic
count of everything ever written. The writer never waits and never consults the reader.
The reader works out from the count which *absolute* sample indices it is about to copy,
copies them, and asks the count again — if the writer has moved on by more than the ring
holds, it says so and the display keeps the frame it had. The slots are
`std::atomic<float>` rather than a plain array, which is the whole safety argument and
not a decoration: copy-then-check over a plain array is a data race, undefined behaviour
rather than a merely stale value, and a relaxed atomic load of a float is one instruction
anyway.

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
brew bundle                          # cmake >= 3.25, ninja, ccache, lame
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

Catch2 via CTest. `ctest --preset release` runs all 433.

MP3 is the one thing here that needs a tool dew does not ship. JUCE can only decode MP3
on its own, so encoding drives an installed `lame` binary as a child process - which is
why it adds no build dependency and no licence question. Without it the format simply
reports itself unavailable and the dialog says so; everything else works regardless.

`dew_render` loads a project and renders it with no audio device, which is how
playback correctness is checked without ears:

```sh
dew_render examples/demo.dew out.wav              # the arrangement, once, plus its tail
dew_render examples/demo.dew out.wav --pattern 1 --seconds 8
dew_render examples/demo.dew out.flac --format flac
dew_render examples/demo.dew out.mp3 --format mp3 --mp3-quality 18
dew_render examples/demo.dew out.mid --format midi
dew_render examples/demo.dew out.wav --bars 5:9 --normalize --peak -1
dew_render examples/demo.dew --stems stems/       # one file per mixer track
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
dew_shot tabs out --project examples/effects.dew     # one PNG per tab
dew_shot gallery out.png                             # the design system
dew_shot audio out.png                               # the audio settings panel
dew_shot midi out.png                                # the MIDI settings panel
dew_shot render out.png --format midi                # the render dialog
dew_shot randomize out.png                           # the piano roll's randomize dialog
```

It exists because screen-recording permission is not always available, and because a
layout defect is obvious in a picture and nearly invisible in code. Both bugs reported
against the first version of this app — "channel rack clicks don't go through" and
"can't add more patterns" — turned out to be layout and affordance problems that two
rounds of code-reading had missed and one PNG made obvious.

## Licence

JUCE 9 is dual-licensed: commercial, or AGPLv3. This project is a personal prototype
used under the **AGPLv3** terms. See [THIRD_PARTY.md](THIRD_PARTY.md).
