# dew

A desktop digital synth DAW in the FL Studio shape: a channel rack with a step grid, a
piano roll, a playlist of clips and a mixer, driven by a three-oscillator synth per
channel — or, on an audio channel, by a recording.

Status: **working prototype**. New, open, edit, save and playback work end to end, with
effects and automation on top. It is not a product, but every layer is real and wired to
the next. macOS only: nothing has been built or run on another platform.

## Build

```sh
brew bundle                    # cmake >= 3.25, ninja, ccache, lame
cmake --preset release
cmake --build --preset release
```

Presets, not raw flags:

| Preset | What it is |
|---|---|
| `dev` | Debug, with tests |
| `release` | RelWithDebInfo — build this for normal use |
| `ci` | `release` plus warnings-as-errors; the gate |
| `offline` | `release` from a warm dependency cache, no network |

The first configure fetches JUCE and Catch2 from source — a few seconds, shallow, cached
per machine under `~/.cache/CPM` — so one build is slow and the rest are not.

## Run

```sh
open build/release/src/dew_artefacts/RelWithDebInfo/dew.app
```

Then **Demos → Getting Started** in the menu bar; there are four.

Two command-line tools build alongside it, both into
`build/release/tools/<name>_artefacts/RelWithDebInfo/`. Neither needs an audio device or
a display, which is how the app is tested.

**`dew_render`** loads a project and writes audio:

```sh
dew_render examples/demo.dew out.wav                     # the arrangement, plus its tail
dew_render examples/demo.dew out.wav --bars 5:9 --normalize
dew_render examples/demo.dew out.mp3 --format mp3
dew_render examples/demo.dew --stems stems/              # one file per mixer track
dew_render --help                                        # scope, format, dynamics
```

It exits non-zero on a silent render, because "loaded but made no sound" is the failure
it exists to catch.

**`dew_shot`** paints the UI into a PNG with no window:

```sh
dew_shot editor out.png --project examples/melody.dew --tab piano-roll
dew_shot tabs out                    # one PNG per tab
dew_shot gallery out.png             # the design system
dew_shot --help                      # and the four dialogs
```

Screen-recording permission is not always available, and a layout defect is obvious in a
picture and nearly invisible in code.

## Test

```sh
ctest --preset release        # 859 tests
```

The gate, which is what CI runs and what a change has to pass:

```sh
cmake --build --preset ci && ctest --preset ci \
  && ./scripts/check-deps.sh && git diff --exit-code -- THIRD_PARTY.md
```

`release` is not the gate. Only `ci` builds warnings-as-errors, so it is the only one
that catches an exact float comparison or a dropped result.

MP3 is the one thing needing a tool dew does not ship: JUCE can only decode it, so
encoding drives an installed `lame` as a child process — no build dependency and no
licence question. Without it the format reports itself unavailable and the rest works.

## What it does

- **Channel rack** — a step grid, one row per channel; click or drag to write steps.
  Volume, pan, mute and solo on the row itself, so a pattern is balanced where it is
  written. A pattern wider than the panel scrolls, and zooms.
- **Piano roll** — the *same notes* as the step grid, in a second view: a lit step is a
  note at the channel's base pitch. Rubber-band select, move a chord without losing its
  shape, drag velocity bars in the lane below, click the keys to hear them. A new note
  takes the length and velocity of the last one drawn. Tools: select, paint and slice, a
  snap grid from 1/16 to a bar, quantize, transpose, and a randomize dialog. Everything
  except the tools acts on **the selection if there is one, and the whole channel
  otherwise**.
- **Playlist** — pattern clips on tracks along a bar timeline; a clip longer than its
  pattern repeats it. Drag clips between tracks, double-click to open a pattern, mute or
  solo a lane.
- **Automation** — clips on the playlist driving a declared set of targets: channel and
  track volume and pan, master gain, and any effect parameter. Drag points, double-click
  to add, alt-click to remove.
- **Mixer** — a fader, pan, mute, solo and a peak meter per insert, plus master; below
  them, the effect chain of the selected strip. Each strip lists the channels routed into
  it, and clicking one goes there. Solo is resolved across the whole mixer.
- **Effects** — reverb, filter, delay, drive, chorus and a 3-band EQ, chained up to four
  deep on any channel, mixer track or the master. One editor pointed either way round: an
  accordion down the instrument panel, a row of open cards across the mixer. Drag a card
  by its grip to reorder: it lifts and follows the pointer, the rest of the chain parts to
  open a gap where it would land, and the move happens when you let go — one undo step for
  the whole drag, however far it travelled. Escape abandons it, and so does letting go
  outside the chain.
- **Instrument** — three band-limited oscillators (sine/saw/square/triangle) or
  wavetables with unison, each with its own octave, detune, gain and on/off switch. One
  ADSR envelope behind them, and channel volume and pan.
- **Recording** — **+ Audio** adds a channel that plays a recording instead of its
  oscillators. Arm with **R**, press Record, and the take lands on the playlist at the
  bar the playhead was on. Its rack row shows the waveform; the instrument panel shows
  trim handles, fades, pitch, reverse and loop. Everything downstream — routing, volume,
  pan, effects — is shared with a synth channel.

  The microphone is asked for when an input is chosen or a channel is armed, never at
  startup: dew opens the device output-only, because a permission prompt that arrives
  before you have asked for anything is one people refuse.
- **Loop** — a span selected on any ruler is the span that plays; the transport wraps
  inside it. A loop drawn ahead of the playhead is played into rather than jumped to.
- **Transport** — play/stop, tempo, time signature, a pattern-or-song switch, and pattern
  add/duplicate/delete with an editable length. The position indicator stays on screen
  while stopped, dimmed, so Stop visibly returns it to the start rather than hiding it.
- **Time signature** — 3/4, 5/4, 6/8, 7/8 and the rest, driving bar lines, beat shading,
  the bars:beats:ticks readout, what "Bar" snaps to, and exported MIDI. It is a *metre*,
  not a tempo: a step is the same length in 3/4 as in 4/4. Because a clip is stored in
  bars, changing the signature rescales the arrangement with it, and says so when a clip
  had to round.
- **Visualisation** — an oscilloscope and spectrum of the master output on the transport
  bar, triggered on a rising zero crossing so the trace stands still. Hidden below about
  a thousand pixels of window.
- **Render** — the song, one pattern, or a span of bars, as WAV, FLAC, MP3 or MIDI, with
  sample rate, release tail, normalize, fades, dither and stems. Runs off the message
  thread. **File**, or ⌘E.
- **Audio settings** — driver, output, input, input channels, sample rate and buffer
  size, with the resulting latency, an input meter and a test tone. **Audio**, or ⌘,.
- **It remembers** — window geometry, active tab, selections, the piano roll's zoom,
  scroll and snap, panel width and chosen device. The piano roll's *tool* deliberately
  does not: restoring into paint or slice would mean the first click of a session cuts
  something nobody asked for.
- **Score** — a fifth tab holding a text description of the whole song: key, meter, chord
  progression, sections, and per channel a voicing, a melody or a counterpoint answering
  another voice. It is checked as you type - squiggles where it is wrong, a clickable list
  of what is wrong, Control-Space to complete - and compiled into real patterns, notes and
  clips on ⌘R. The source is stored in the `.dew`, so recompiling
  updates what it wrote last time and leaves anything you have since edited by hand alone.
  See **The score language** below.
- **File** — New, Open, Save, Save As, dirty tracking, a save-before-closing prompt, and
  undo/redo over every edit.

## Keyboard and mouse

⌘N ⌘O ⌘S ⇧⌘S · ⌘E render · ⌘R compile score · ⌘Z ⇧⌘Z · Space play · R record · ⌘L
pattern/song · ⌘K add channel.

⌘1 – ⌘5 go to the channel rack, piano roll, playlist, mixer and score; ⌃⇥ and ⌃⇧⇥ cycle
them, and ⌘\ folds the instrument panel away. They are in the **View** menu, which is
where the rest of them are too.

Every one of those is a row in `src/ui/Hotkeys.h`, and so is every key the timeline
editors read. There used to be two key tables that could not see each other — the menu
bar's and the editors' — and between them ⌘1 was swallowed by whichever editor had focus
and `R` meant two different things. A source-scanning test now refuses a key spelled
anywhere else.

In a timeline editor — the step grid, the piano roll, the playlist — `+` `-` `0` zoom in,
out and to fit. They read one keyboard map, so a key that means something in two of them
means the same thing in both; each implements the commands it has, and the map is not a
promise that every view has every command.

The step grid takes keyboard focus when you click it, which it never used to: its zoom
keys were live in the tests and nowhere else.

| | step grid | piano roll | playlist |
|---|---|---|---|
| `+` `-` `0` zoom | ✓ | ✓ | ✓ |
| `1` `2` `3` tools | — | select · paint · slice | select · paint |
| `esc` clear selection | — | ✓ | ✓ |
| `del` delete selection | — | ✓ | — |
| ⌘A select all | — | ✓ | — |

Scrolling and zooming: wheel to scroll, ⌘-wheel or a trackpad pinch to zoom around the
pointer, shift-wheel to scroll in time. Natural scrolling is honoured, because the system
reports it rather than applying it.

Dragging a value: **shift is finer**, on every knob, fader and number field. Shift means
five other things in dew — suspend snap, extend a selection, make a copy unique, transpose
an octave — and every one of them changes a *selection* or a *position*. None changes a
value. That is what keeps the sixth meaning from being one too many. A whole drag is one
undo step.

Right-drag erases in the piano roll and the step grid, and means the same thing in both:
one undo step for the sweep, filling the cells between drag samples so a quick flick
leaves no survivors. Alt-drag is the same gesture. A right-press that erased nothing was
never an erase, so in the piano roll it clears the selection instead. In the playlist,
right-click opens a menu instead — a clip is an object with properties and a step is not.

On any ruler: drag to scrub, shift-drag to select a span, ⌘-click to span from the
playhead, shift-click or double-click to drop it. **The span is what plays.**

Right-click a rack row or a track header to rename, add or remove it. **+ Channel** and
**+ Track** sit under the last one, where the next will appear.

In the piano roll: ↑ ↓ transpose a semitone and ⇧↑ ⇧↓ an octave; Q quantizes, ⇧R opens
randomize — bare `R` is Record, which has to work from wherever you happen to be looking. Holding shift suspends the snap grid for a drag, which is the only way to
reach an off-grid position without changing the dropdown.

## Architecture

Six layers, each a static library, each testable without the ones above it. Libraries
rather than directories on purpose: the include graph was already acyclic, but nothing
enforced it, and the headless `dew_render` linked all thirty UI translation units to
write a WAV. Split, a layering mistake is a link error.

```
dew_ui       ChannelRack · PianoRoll · Playlist · Mixer · TransportBar · StatusBar
             EffectChainHost → EffectChainComponent · settings panels · toolbars
             TimelineView (step↔pixel map) · TimelinePaint · Gestures · HeaderRow
               │ every edit goes through ProjectEdits — one undo step per gesture
             ├────────────────┬──────────────────┬─────────────────┐
             ▼                ▼                  ▼                 │
dew_design   tokens · icons · primitives · look and feel · Animator │
             SignalScope                                           │
                              │                                    │
dew_app      Settings (window, view and device state, validated on read)
               A leaf, not a top: it holds state the UI reads.     │
                                                 ▼                 │
dew_io       LiveAudioHost (a device) · MidiInputHost + MidiRouter (a controller)
             SamplePool (files) · OfflineRenderer + RenderJob + MidiExporter
             AudioRecorder (a device and a file)
                                                 │
                                                 ▼
dew_engine   SnapshotBridge → Transport → Sequencer → InstrumentModule[]
                            → EffectModule[] → MixerBus
             Opens no files and no devices — and cannot: it links neither
             juce_audio_devices nor juce_audio_formats, which a test asserts.
                                                 │
                                                 ▼
dew_model    ProjectDocument (FileBasedDocument) ── ValueTree ── ProjectSchema ── JSON
             ParamSpec + ModuleCatalog (every parameter, declared once)
             AutomationTargets · NoteTools (snap, quantize, slice — no GUI)
             A leaf: it depends on nothing of dew's.

             │ AsyncUpdater coalesces rebuilds ─→ EngineSnapshot
             │ PreviewQueue carries auditioned notes
```

Several tests enforce a convention by scanning the sources, and each passes silently when
it finds nothing; `SourceGateTests` checks that walk against the libraries' own source
lists, so code that moves out of `src/` cannot quietly disarm them.

### Instruments and effects are modules

Each is a class behind a small interface — `prepare` / `reset` / `process` /
`releaseResources` — that maps one-to-one onto `AudioProcessor`, so hosting dew's effects
elsewhere later is a wrapper rather than a rewrite. There is no VST3 SDK here and no
plugin hosting; the boundary is drawn so that adding them does not mean starting again.

Deliberately no `get/setStateInformation`: a dew module owns no state the document owns.
Every parameter lives in the `ValueTree`, and DSP state is not persisted — so
serialisation is a property of the *descriptor*, generic over every type.

A parameter is declared once, as a `ParamSpec` in `ModuleCatalog`. The schema's defaults,
the engine's clamps, the automation ranges and the UI's controls are all views of that
one row. They used to be four tables that had drifted: `cutoff` stopped at 18kHz in two
of them and 20kHz in the engine, and a mixer fader offered 0–1.5 against an engine clamp
of 2.0 and an automation range of 0–1, so automating a fader swept two thirds of it and
stopped.

### Effects without a command queue

Effect *parameters* travel in the snapshot like everything else. Effect *instances* own
state — delay lines, reverb tanks — that has to survive a snapshot swap, and the usual
answer is a lock-free command queue.

dew does not have one. Modules are built on the message thread on first use, keyed on
(slot, type), and **never destroyed while the engine lives**. That is a correctness
argument rather than thrift: `SnapshotBridge` has no acknowledgement path, so the message
thread cannot learn when the audio thread has finished with an old snapshot, and anything
a snapshot points at must outlive every snapshot. Freeing an "unused" module hands the
audio thread a dangling pointer that no test catches reliably.

Slots are keyed on each effect's persistent id by open addressing, so the mapping is a
pure function of the ids in the document: adding an effect to an earlier channel does not
renumber the later ones and cut the reverb tail they were in the middle of.

### Three queues, three different contracts

Clicking a piano key has to make a sound without the sequencer running, so preview notes
reach the audio thread through **`PreviewQueue`** — a single-producer ring, not a
latest-wins atomic, because both halves of a fast click can land inside one 5.8ms block
and latest-wins would let the release overwrite the press.

**`SnapshotBridge`** hands over whole states, because half a state is nonsense: a
triple-buffered publish through a single compare-and-swap.

**`SignalTap`** is the only thing that runs audio → message, and it wants the opposite of
both: a visualiser needs the newest two thousand samples and nothing older. So it is a
ring that overwrites without asking, plus one monotonic count. The writer never waits.
The reader works out which *absolute* sample indices it is copying, copies them, and asks
the count again — if the writer has moved on by more than the ring holds, the display
keeps the frame it had. The slots are `std::atomic<float>` rather than a plain array, and
that is the safety argument rather than a decoration: copy-then-check over a plain array
is a data race, which is undefined behaviour rather than a merely stale value.

The engine knows nothing about audio devices. It is prepared with a sample rate and a
block size and fills a buffer, so live playback and offline rendering run the same code
and a passing render says something about the real engine.

## Design system

`src/ui/design/` holds the vocabulary — colour roles, spacing, type, a size ladder, an
*emphasis* scale, motion durations, and thirty-odd icons drawn as `juce::Path` rather
than shipped as assets. `src/ui/primitives/` holds the controls built on it.

`dew_shot gallery out.png` renders every token, icon and primitive in every state onto one
page, which is both how the design system is reviewed and how it is tested.

Eight source-scanning tests keep the vocabulary whole, and each one exists because a
second vocabulary had grown beside the first: no colour written as hex, no emphasis as a
bare number, no radius or stroke as a bare number, no gap or inset off the spacing scale,
no component redeclaring a dimension the ladder already names, no timer picking its own
refresh rate, no font built outside `Tokens.cpp` — and one that refuses the opposite
mistake, a token nothing refers to.

### Motion

Every eased value is stepped from one clock. Two decisions shape it.

It is a `juce::Timer` rather than a `VBlankAttachment`, because a vblank needs a
`ComponentPeer` and every UI test here paints into an `Image` with no peer and no message
loop: a design that cannot run where the suite runs is one the suite cannot check.

And **animation is off unless the application turns it on** — deliberately the wrong way
round from how it looks, so every headless test and every `dew_shot` render behaves
exactly as it did before the animator existed. Motion is a property of a running
application, not of a widget. `Animator::advance (deltaMs)` steps every client by a
chosen number of milliseconds with no wall clock, so a test walks a whole interaction
frame by frame rather than sampling it at the ends. Reduce motion sets every duration to
zero, which makes `animateTo` identical to `snapTo` — no call site needs a branch.

A knob has three rules, in priority order: animation is off unless turned on; a **drag is
never eased**, because a needle trailing the pointer moving it feels broken; and the
**first** value a knob is given snaps, or a panel built from a document sweeps every knob
up from zero. The wheel is never eased at all — adding lag to the one gesture that must
feel direct is a regression, not a polish.

## The score language

A song can be written as text and compiled to notes. It is edited in the **Score** tab, the
fifth one, and compiled with the button there or Command-R; `examples/amber.score` is the
one CI compiles and renders, and `dew_score` is the same compiler on the command line.

```
song {
  title "Amber"
  tempo 96 bpm
  meter 4/4
  grid  auto              // the compiler derives stepsPerBeat from the durations written
  key   F minor
  seed  0x5EEDC0FFEE
}

channel pad {
  mixer    1
  range    C3..C5
  velocity 68 +- 6        // deterministic jitter, not noise
}

channel lead {
  mixer 2
  range F4..A5
}

voicing warm { size 4 voices }    // one line holds one statement
rhythm  held  { 1/1 }
rhythm  pulse { 1/4 1/4 1/2 }

harmony lament {
  i x2 | bVI | bVII | i^1 x2 | iv | V7/iv
}

section verse {
  length 8 bars
  harmony lament
  part pad {
    chords with warm
    rhythm held
  }
  part lead {
    melody {
      rhythm   pulse
      contour  arch
      strong   chord-tones
      variance 0.25
      mute     1 of 4
    }
  }
}

arrangement {
  verse
  verse as verse_b        // a pinned instance, immune to renumbering
  verse x2 identical      // one pattern, two clips
}
```

Seven block keywords, and the set is closed: `song`, `channel`, `voicing`, `rhythm`,
`harmony`, `section`, `arrangement`. **There is no expression language** — no arithmetic,
no variables, no conditionals. That single restriction is what makes completion a table
lookup rather than type inference, and what lets the grid be computed statically before
anything is generated. The moment `length 4 * verses` is legal, both are gone.

`p/q` is always a note value — `1/8t` is an eighth-note triplet, `1/4.` a dotted quarter.
`xN` is a share of what is left over, anything else is an exact length, and the two are
different token kinds so they cannot be confused. `|` is a Lilypond-style bar-line
*assertion*, checked once the shares are known; it is the single highest-value error
catcher in the syntax, because it turns "the section length changed and everything
shifted" into one message. In a chord, case carries quality, `^` carries inversion and `/`
carries tonicisation — using `^` for the inversion is what frees `/` for `V7/iv`. An
uppercase A–G starts an absolute chord and a `b`, `#` or roman letter starts a numeral,
which never collide because I and V are not note letters.

Statements are newline-terminated, one per line. Ending one at the next word the schema
knows would allow several per line, but it puts the schema inside the parser and makes a
typo vanish: `mixor 1` would be absorbed as two more values of the previous statement and
reported as "too many values for `instrument`" rather than "unknown key `mixor`". Comments
are `//` rather than `#`, because `#` is a sharp and a colour prefix, and there are no
block comments or multi-line strings — the editor may restart tokenising at the beginning
of any line, so a construct needing cross-line state is either wrong or expensive.

### What the language cannot say, and why

dew stores one tempo and one meter for a whole project, so **a section in 3/4 inside a 4/4
project is not expressible**. The compiler says so in those words rather than as a grammar
error. A note's position is a whole number of steps and `stepsPerBeat` runs to 16, so the
grid is the least common multiple of what the durations need: sixteenths and eighth-note
triplets meet at twelve, and a thirty-second against any triplet needs twenty-four and is
refused — naming *both* durations, because either alone would have been fine.

Applying a score to an existing project refuses on a grid or meter mismatch rather than
performing it. `stepsPerBeat` owns how long a step is, and `ProjectEdits::setMeter`
rescales every clip and rounds, so either would silently move an arrangement the score did
not write.

### Determinism

`seed` is the root of a tree of keys, and every random choice draws from a stream derived
from its own **structural path** — section, instance, channel, site, bar, onset — never
from a byte offset and never from a shared stream. So editing one section leaves every
other section's notes bit-identical, inserting a blank line changes nothing, and adding a
`choose` at bar 3 cannot rewrite bar 4. `variance 0` is a pure argmin: the same notes
every compile, from any seed.

A melody can also be told how far it may jump and whether a jump has to be answered
(`leap max 7 resolve step`), and whether its rhythm restarts at each bar line or runs on
against it (`align bar` / `align continuous`). A voicing can be told which note goes at
the bottom — `bass from-inversion` is the default and is what makes writing `i^1` move a
note rather than decorate the page.

splitmix64 and PCG32 are written out rather than delegated. `std::uniform_int_distribution`
and `std::shuffle` specify their *statistics*, not their algorithms, so libstdc++ and
libc++ render different music from one seed; `juce::Random` is an LCG whose exact sequence
would become part of the file format. A test pins literal outputs, because those numbers
are now the format.

### The shape of it

`src/lang/` is `dew_lang`, a static library that links **nothing at all**, JUCE included —
a compiler that cannot reach into the document is one whose only output is its IR. A source
gate enforces it, because a header-only include would still link. The parser is hand-written
recursive descent; a generator would have cost a pinned dependency, a manifest row and a
Java-at-build-time decision for fifteen productions, and would have made byte-accurate
diagnostics harder rather than easier. Same argument as "Why not vcpkg or Conan" below.

Output is **baked** into the project as ordinary channels, patterns, notes and
`kind="pattern"` clips, so playback, the piano roll, the renderer, stems and MIDI export
all work on it unchanged and no new clip kind exists to be taught to the four places that
would need it. The language owns notes, patterns and clips; the user owns channels,
instruments, effects and the mixer — a track adopts a channel by name and reads nothing
from it but the name, so a sound you dialled in survives a recompile.

### Counterpoint, and choices a seed makes

A part may answer another rather than being written on its own:

```
part answer {
  counterpoint against lead {
    rhythm               pulse
    parallel-fifths      forbid
    parallel-octaves     forbid
    voice-crossing       forbid
    dissonance-on-strong soft 3
    leaps                soft 1.5
  }
}
```

A **beam search of width 8** over the onsets, scored against the voices already
written — not a constraint solver. A solver's failure modes are "unsatisfiable" and
"twenty seconds", both fatal in an editor that recompiles as you type; the cases a beam
loses are close to inaudible next to the machinery; and a beam's choice can be explained
in a diagnostic. Cost is additive along the timeline, so the beam is an exact dynamic
program over the states it keeps.

The seven rules are a closed set, each `forbid` or `soft <weight>` — closed because
completion depends on it, and because an open-ended rule language is a solver by another
name. When the hard rules leave nothing to sing they are **given up in a declared order,
one at a time, and every one is reported by bar**. The line never falls silent without
saying so. `species` is deliberately absent: Fux's rules are the easy fifth of it, and a
number in the language would imply a guarantee this cannot make.

**Imitation is an operator, not a search target:**

```
part echo {
  imitate lead {
    delay     1 bar
    transpose 2
    mode      diatonic     // stays in the key; `chromatic` moves exactly
  }
}
```

A beam search will essentially never *discover* imitation, because imitation constrains
the whole line's identity rather than local transitions — so asking a search for it is
asking for the one thing it cannot do. Written out it is exact, and it is fifteen lines.
Anything falling past the section's end is dropped rather than wrapped: a canon that
wrapped would answer itself from the future.

**A value can be chosen rather than set**, and say how often it is re-drawn:

```
cadence  choose [1 3 5] per instance   // a different ending in each verse
velocity 80 +- 20 per bar
```

The scope *is* the identity of the draw, not a knob on how random it is: `per song`
derives one key for the whole song, `per instance` one for each rendered instance, `per
bar` and `per note` go deeper. Same seed tree, different depth. `choose` takes a list and
nothing computable — the moment a value can be *computed*, completion stops being a table
lookup and the grid stops being statically knowable.

### The editor

Two things happen in the Score tab and they are deliberately not the same thing.
**Checking** runs on a debounce as you type: it lexes, parses, resolves and generates, and
it writes nothing - no notes, no undo entry. **Compiling** happens only when you ask, and
writes patterns, notes and clips in one undo transaction. A debounced auto-compile would
put an undo step full of notes on every pause in typing and would replace hand edits
without being asked, which is the one thing the recompile policy exists to prevent. The
source text itself *is* saved on the debounce, one transaction per typing run.

Errors surface in three places doing three jobs: the squiggle says **where**, the list
under the editor says **what** and scrolls the editor to it when clicked, and the status
bar says whether the project was written to at all.

Control-Space completes. Keys and block keywords come from the same schema table the
resolver validates against, names from the same resolve the compiler runs, and chords
through the same `resolveChord` that writes the notes — so nothing can be offered that the
compiler would then reject, and a key cannot be added without being completable. Chords are
ranked by the key that is written and **spelled beside the numeral**: in A minor `bVI`
reads `F`.

The highlighter is not a second grammar. `lang::scanOne` is a template over a minimal
cursor concept, and the editor's tokeniser is its second instantiation - the first walks a
`std::string_view` for the compiler, this one walks a `juce::CodeDocument::Iterator`. A
test asserts both produce the same token kinds over the example score, because a
highlighter that disagrees with the compiler is worse than none. Keywords are coloured from
the schema table rather than from a keyword list, so a key cannot exist without being
highlighted.

Compiling into a project that already has music refuses a grid or meter mismatch, as
described below. Compiling into an *empty* one applies both: nothing there has a meaning
they could change, and a new project sits at four steps per beat.

### Recompiling, and what happens to what you changed

The source lives **inside** the `.dew`, one node per line so it reads as a diff rather than
as one enormous string. A project and the score it came from are one document; the moment
they can travel separately, "which of these two files is current" becomes a question
somebody has to answer.

So compiling twice is an update, not a second copy. Every node a compile writes carries a
`genId` naming the part of the score that produced it — a section plus either its
occurrence number or its `as` label, the same identity the random draws use, so what pins
an instance's music also pins its document node. A pattern also carries the hash its notes
had when they were written. Recompiling hashes them again, which sorts every generated
pattern into three:

| State | What happens |
|---|---|
| hash matches | nobody has touched it — replaced |
| hash differs | edited in the piano roll — **kept**, counted, and reported |
| no longer produced | removed, unless it was edited, in which case it stays without a clip |

`dew_score --discard-edits` takes the other branch. Both are one undo transaction either
way. The hash covers the length and the notes and deliberately not the name: renaming a
pattern is not a musical change, and letting it read as one would mean labelling a pattern
quietly stopped the compiler ever updating it again.

The honest limit: a clip on the generated lane is rebuilt every time, because where a
section sits is the arrangement's to say. Drag one to another lane and it is yours.

## The project file

One JSON file, `formatVersion`-stamped. The schema is declared once in
`src/model/ProjectSchema.cpp` as a table of node specs with per-property defaults, and
that table drives reading, writing and validation — so there is no hand-written writer to
drift out of step with a hand-written parser.

- A property absent from the file takes its default, so older files load.
- A property of the wrong type takes its default and warns, rather than failing.
- A key the schema does not know is dropped and reported.
- A newer `formatVersion` is refused outright instead of half-read.

Saving writes to a temporary and swaps, so an interrupted save cannot destroy the project
it was overwriting.

Audio is the one thing the file cannot hold. A project that references recordings gets a
sidecar folder beside it — `Song.dew` and `Song Assets/` — and stores paths relative to
itself, so the pair can be copied elsewhere intact. Saving gathers: every referenced file
not already in the sidecar is copied in and its path rewritten. A sample imported from
elsewhere on the disk keeps its absolute path rather than becoming a chain of `../`,
which would be portable to nothing.

## Dependencies

Two — JUCE and Catch2 — fetched from source by
[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake).

**Versions live in exactly one place**, [`cpm-package-lock.cmake`](cpm-package-lock.cmake).
**Identity lives in one other**, [`cmake/DependencyPins.cmake`](cmake/DependencyPins.cmake),
which records the commit each tag must resolve to.

The split exists because the two goals conflict. CMake's `GIT_SHALLOW` clones with
`--depth 1 --no-single-branch`, so it can only reach branch tips — a raw commit hash is
not reliably fetchable. But a tag is mutable and a commit is not. So dew fetches shallow
**by tag** (JUCE this way is 19 MB in ~3 s instead of a 288 MB clone) and then **verifies
the resolved commit** during configure. A re-pointed upstream tag fails the build loudly
instead of silently changing what gets compiled, and the check fails closed: a source tree
whose identity cannot be established is an error, never a skip.

- **CPM is vendored**, not downloaded — 45 KB, sha256-verified against upstream's
  published hash and re-checked by `check-deps.sh`. Bootstrapping needs no network.
- **`./scripts/check-deps.sh`** rejects floating refs (`master`, `HEAD`, `origin/*`),
  missing pins, non-git sources and a modified CPM.
- **[`THIRD_PARTY.md`](THIRD_PARTY.md)** is generated at configure time from what was
  actually resolved — libraries, commits, licences and the toolchain — so it cannot drift
  from the lock. CI fails if the committed copy is stale.

To bump a dependency: change `GIT_TAG` in the lock, resolve the new commit with
`git ls-remote <repo> 'refs/tags/<tag>*'` (take the `^{}` line for annotated tags), paste
it into `DependencyPins.cmake`, and reconfigure.

Not vcpkg or Conan: vcpkg's `juce` port is at 8.0.7 with ten patches and a devendored
Oboe, so JUCE 9 would mean maintaining an overlay port, and Conan Center has no JUCE
recipe at all. Both buy binary caching and transitive resolution that a two-dependency
project does not need. If the graph grows past a handful, vcpkg manifest mode with a
`builtin-baseline` is the migration target.

## Licence

JUCE 9 is dual-licensed: commercial, or AGPLv3. This project is a personal prototype used
under the **AGPLv3** terms. See [THIRD_PARTY.md](THIRD_PARTY.md).
