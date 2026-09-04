# Architecture

Eight static libraries. **There is no `dew_core`** — anything that says so is out of date.
The layering is a link error, which is the whole reason they are libraries rather than
directories; [README.md](../../README.md#architecture) draws the diagram and argues that
part, and [Why it is this way](#why-it-is-this-way) below covers modules and the project
file.

| Library | Directory | May depend on |
| --- | --- | --- |
| `dew_lang` | `src/lang/` | **nothing at all**, JUCE included |
| `dew_i18n` | `src/i18n/` | `juce_core` only — so a catalogue cannot open a file or build a `ValueTree` |
| `dew_model` | `src/model/` | `dew_lang`, `dew_i18n`, JUCE data structures and graphics |
| `dew_engine` | `src/engine/` | `dew_model`. Neither `juce_audio_devices` nor `juce_audio_formats` |
| `dew_io` | `src/io/` | `dew_engine`. Everything that touches a file or a device lives here |
| `dew_design` | `src/ui/design/`, `src/ui/primitives/` | JUCE. Knows nothing about a project |
| `dew_app` | `src/app/` | `dew_model` only — so it **cannot see `Tokens.h`** |
| `dew_ui` | `src/ui/` | all of the above |

Consequences you will hit:

- **Includes are rooted.** `#include "model/Ids.h"`, never `../model/Ids.h`. A gate
  refuses a relative cross-layer include, and another refuses the same header twice.
- **`dew_engine` opens no files and no devices**, and cannot: a gate asserts the link line.
  If the engine needs a sample or a SoundFont, it takes a provider interface
  (`SampleProvider.h`, `SoundFontProvider.h`) and `dew_io` supplies it.
- **`dew_app` cannot clamp to a token.** `Settings` stores such a number raw and the view
  clamps; restating the bounds in `Settings.h` trips the size-ladder gate.
- **Adding a library** means three edits, not two: the `foreach` in `tests/CMakeLists.txt`,
  the layer list in the gate "every layer is represented in the scanned sources", *and*
  the `directDeps` map in "no layer includes a header a layer above it owns". Miss one of
  the first two and the gates silently stop covering it; miss the third and the gate throws
  an exception that names nothing — there is a `REQUIRE` there now that names it instead.
- **Only `dew_model` names `dew_i18n`**, PUBLIC, and every layer above inherits it. Naming
  an inherited library a second time makes every link warn about a duplicate.

## The document

- **A parameter is declared once**, as a `ParamSpec` in `src/model/ModuleCatalog.cpp`. The
  schema's defaults, the engine's clamps, the automation range and the UI's control are
  all views of that row. Never add a second table.
- **Never hand-build a `ValueTree` node.** Use `defaultTreeFor (spec)`
  (`src/model/ProjectSchema.h`). A hand-built node stops round-tripping the moment the
  schema gains a property, and the canonical-shape test in `ProjectEditsTests.cpp` is what
  catches it.
- **Every edit goes through `ProjectEdits`** (`src/model/ProjectEdits.h`, implemented
  across `src/model/edits/`), one undo step per gesture. A gate refuses a source that
  writes an undoable property by hand.
- **Modules own no state the document owns.** There is deliberately no
  `get/setStateInformation`: serialisation is a property of the *descriptor*, written once
  over both descriptors in `src/model/ModuleState.*`.
- **The schema is one table.** `src/model/ProjectSchema.cpp` drives reading, writing and
  validation, so there is no hand-written writer to drift from a hand-written parser.

## Two things that fail silently

- **A new clip `kind` has to be taught to four places, and three say nothing when it is
  not.** `EngineSnapshot::isSilent` (whether `dew_render` thinks there is anything to do)
  and `songLengthSteps` (how long the arrangement is) are a pair; `OfflineRenderer` builds
  its **own** snapshot, so anything `buildSnapshot` needs must be threaded through
  `RenderOptions` or every render and stem export drops that content without a word. Only
  `PlaylistComponent::paint` is loud about being incomplete. Check `openPatternOf` too —
  every clip carries `patternId`, so a kind that does not use one will open pattern 1.
- **A metre is not a tempo.** `src/model/Meter.h` is the only place that knows
  `beatsPerBar` is load-bearing (it is `stepsPerBar` for the whole app) and `beatUnit` is
  notational — it names the metre, labels the snap divisions and goes into the MIDI time
  signature, and **must never reach `Transport::samplesPerStepFor`**. A clip is stored in
  bars, so `ProjectEdits::setMeter` rescales `startBar` / `lengthBars` / `barsInSong` in
  the same undo transaction; without that a 1-bar clip of a 16-step pattern spans 12 steps
  in 3/4 and the pattern's last four steps stop sounding.

See also [realtime.md](realtime.md) for what the engine may do on the audio thread, and
[automation.md](automation.md) for the parameter-to-curve path.

## Why it is this way

### Instruments and effects are modules

Each is a class behind a small interface — `prepare` / `reset` / `process` /
`releaseResources` — that maps one-to-one onto `AudioProcessor`, so hosting dew's effects
elsewhere later is a wrapper rather than a rewrite. There is no VST3 SDK here and no
plugin hosting; the boundary is drawn so that adding them does not mean starting again.

Deliberately no `get/setStateInformation`: a dew module owns no state the document owns.
Every parameter lives in the `ValueTree`, and DSP state is not persisted — so
serialisation is a property of the *descriptor*, generic over every type. That is
`stateFor` and `validateState` in `ModuleState`, written once over both descriptors, and a
preset is one call to each.

Both kinds of module have a descriptor now. An `EffectDescriptor` joins a type to its id,
its display name and its parameters; an `InstrumentDescriptor` does the same through
`ParamGroup`, which says which *node* a run of parameters lives on — because an effect is
one node with a flat list and an instrument is a channel, three oscillator slots and an
envelope. That asymmetry is named rather than flattened, and `effectGroup` presents an
effect as the degenerate case, so one walk covers both. The schema's `oscSpec`, `ampSpec`,
`channelSpec` and `sampleSpec` are generated from those tables, and a test compares the
committed `examples/` byte for byte against what the factory writes — `isEquivalentTo`
does not compare property order, so without it a reshuffle would leave every example stale
and fail nothing.

A parameter is declared once, as a `ParamSpec` in `ModuleCatalog`. The schema's defaults,
the engine's clamps, the automation ranges and the UI's controls are all views of that
one row. They used to be four tables that had drifted: `cutoff` stopped at 18kHz in two
of them and 20kHz in the engine, and a mixer fader offered 0–1.5 against an engine clamp
of 2.0 and an automation range of 0–1, so automating a fader swept two thirds of it and
stopped.

### Soundfonts

A channel plays its oscillators, a recording, or a **soundfont**. The third is a keyed
multisampler: a `.sf2` file, one sound chosen inside it, and six knobs that *bend* what
the font already says rather than replacing it — a pitch and a tuning, a filter offset,
attack and release multipliers, and how much of the format's velocity curve to apply.
They are offsets because that is the mechanism SF2 itself uses to let a preset colour an
instrument it does not own, and because a preset spanning forty regions that disagree has
no single value a knob could honestly show.

The reader is written here, in `dew_io`, rather than taken from a library. A soundfont
player from outside is a second synth engine with its own envelopes, filter and voice
stealing, and none of its parameters would be `ParamSpec` rows. What was needed is a
reader, and the format's sampler core is bounded.

What it reads and what it cuts was **measured** against a 446-font library rather than
decided from the specification. Modulators appear in four files of 446. Both LFOs are
configured constantly and routed almost never — every destination that makes one audible
is 1.2% of zones — so they are cut. The modulation envelope is not: `modEnvToFilterFc`
alone is 173 zones, more than every LFO route put together, and it is what gives those
fonts their sweeps. Two files in that library are spelled `.SF2`, so every extension
test is case-insensitive.

A soundfont is **referenced, never gathered**. `gatherAssetsInto` copies recordings into
`<Project> Assets/` on save; a font is a library you own, like a plugin, not a take that
belongs to one song — and it can be five hundred megabytes. So a project can arrive
before its fonts, and a channel whose font is missing is silent with a warning.

The file is parsed as something hostile, because it is something a person chose off their
disk: every chunk length, bag index, sample id and loop point is checked against what was
actually read, and a malformed font yields warnings and no presets. `SoundFontCorpusTests`
walks a real library when `DEW_SOUNDFONT_CORPUS` points at one — 448 fonts, 7231 regions,
every one in bounds.

An instrument therefore writes a **stereo** pair. A fifth of the samples in a real library
are one half of a stereo pair, and summing them would flatten all of it. The two mono
instruments widen through a `MonoInstrumentModule` base that renders into a scratch and
adds it into both sides, which leaves every rendered sample bit-identical — the render
tests passed the change with no edit to a single pinned value.


### The project file

One JSON file, `formatVersion`-stamped. The schema is declared once in
`src/model/ProjectSchema.cpp` as a table of node specs with per-property defaults, and
that table drives reading, writing and validation — so there is no hand-written writer to
drift out of step with a hand-written parser.

- A property absent from the file takes its default, so older files load.
- A property of the wrong type takes its default and warns, rather than failing.
- A key the schema does not know is dropped and reported.
- A newer `formatVersion` is refused outright instead of half-read. v12 is current: it
  gave a playlist track and a mixer strip a `colour`, additively, with the empty string
  as the declared default so an earlier file loads looking exactly as it did.

Saving writes to a temporary and swaps, so an interrupted save cannot destroy the project
it was overwriting.

Audio is the one thing the file cannot hold. A project that references recordings gets a
sidecar folder beside it — `Song.dew` and `Song Assets/` — and stores paths relative to
itself, so the pair can be copied elsewhere intact. Saving gathers: every referenced file
not already in the sidecar is copied in and its path rewritten. A sample imported from
elsewhere on the disk keeps its absolute path rather than becoming a chain of `../`,
which would be portable to nothing.

