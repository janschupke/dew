# Architecture

Nine static libraries. **There is no `dew_core`** — anything that says so is out of date.
The layering is a link error, which is the whole reason they are libraries rather than
directories; [README.md](../../README.md#architecture) draws the diagram and argues that
part, and [Why it is this way](#why-it-is-this-way) below covers modules and the project
file.

| Library | Directory | May depend on |
| --- | --- | --- |
| `dew_lang` | `src/lang/` | **nothing at all**, JUCE included — so it carries its own string catalogue |
| `dew_i18n` | `src/i18n/` | `juce_core` only — so a catalogue cannot open a file or build a `ValueTree` |
| `dew_model` | `src/model/` | `dew_lang`, `dew_i18n`, JUCE data structures and graphics |
| `dew_engine` | `src/engine/` | `dew_model`. Neither `juce_audio_devices` nor `juce_audio_formats` |
| `dew_io` | `src/io/` | `dew_engine`. Everything that touches a file or a device lives here |
| `dew_control` | `src/control/` | `dew_io`. The operation table an agent drives dew through — see [mcp.md](mcp.md) |
| `dew_design` | `src/ui/design/`, `src/ui/primitives/` | JUCE. Knows nothing about a project |
| `dew_app` | `src/app/` | `dew_model` only — so it **cannot see `Tokens.h`** |
| `dew_ui` | `src/ui/` | all of the above |

The layering is a DAG rather than a ladder: `dew_control`, `dew_design` and `dew_app` are
siblings that know nothing of each other, which is what stops protocol code reaching
`Tokens.h`.

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
  signature, and **must never reach `Transport::samplesPerStepFor`**. `setMeter` now
  rescales only `barsInSong`: a clip is stored in STEPS (`startStep` / `lengthSteps`, as of
  format v20), so a bar changing size moves nothing. It used to be stored in bars, and the
  rescale that held each clip's position in steps across a metre change is what that
  buys — without it a 1-bar clip of a 16-step pattern spanned 12 steps in 3/4 and the
  pattern's last four steps stopped sounding.
- **`stepsPerBeat` is the other half of the same rule, and moves in the opposite
  direction.** It owns how long a step IS, so `ProjectEdits::setGridResolution` rescales
  every note, every pattern length, every clip and every automation point in one undo
  transaction. The guard is a render pinned sample-for-sample across two resolutions; the
  metre has the twin of it.

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
effect as the degenerate case, so one walk covers both.

`ParamGroup::under` is what nests a group below a slot, and it is independent of the
generator registry: `CLASSIC` and `WAVETABLE` are generators — one of them runs and the
other is inert — while `LFO` is under a slot without being one, because what it moves
belongs to the slot whichever generator is playing. `generatorNodeFor` answers "which node
holds this property" for all three by reading the descriptor, so a fourth such node is a
row in `ModuleCatalog` and no edit anywhere else. Getting that wrong is silent in the
worst way: the write lands as a property on the `OSC` node, `ValueTree` accepts it, the
panel shows it, and the schema drops it on the next save. The schema's `oscSpec`, `ampSpec`,
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

**Declared once is not touched once.** A new parameter is one row, and then ten files that
each have to be told the row exists. The spine, in the order the value travels:

| # | File | What it adds |
| --- | --- | --- |
| 1 | `src/model/Ids.h` | the identifier, which everything below points at |
| 2 | `src/model/ModuleCatalog.cpp` | the `ParamSpec` row: range, default, curve, control, automatable |
| 3 | `src/model/ParamNames.cpp` | its name and its help, as `StringId`s |
| 4 | `src/model/ParamRole.cpp` | what KIND of thing it is, for the automation picker |
| 5 | `src/model/ProjectSchema*.cpp` | nothing, usually — the schema is generated from the tables |
| 6 | `src/engine/EngineSnapshot.h` | a field on the settings struct, and an `AutomationParam` |
| 7 | `src/engine/SnapshotReaders.cpp` | the read, through `clampBySpec` |
| 8 | `src/engine/AutomationOverrides.cpp` | the line that applies a curve to it |
| 9 | `src/engine/` — the voice or the module | what it actually DOES |
| 10 | `src/ui/` — the panel | the control, built from the spec |

Step 8 is the one that fails silently, so it is gated: `AutomationOverrideTests` drives
every `AutomationParam` through `applyAutomation` and requires that something moved. A
parameter with a catalog row, a picker entry and a drawn curve but no line in the override
ladder moves a line on screen and nothing in the sound, and four oscillator parameters
were in exactly that state until somebody tried one.

The last two feature commits touched 32 and 24 files in `src/` over this spine. That is
the cost the single declaration does not remove: it removes the DISAGREEMENT, not the work.

### The FM matrix

A slot's three oscillators can modulate each other's phase. Each carries a row of a 3x4
matrix — how far it bends each slot, and an output column saying how much of it is heard
— edited on a fourth segment of the same selector the slots use, because it is the one
view in that panel that is about all three at once.

**The defaults are the identity, and everything rests on that.** Every amount is zero and
every output is one, which is three oscillators summed in parallel: the synth as it was.
`snapshotRead::anyFmIn` turns the twelve cells into one flag on the bank, and a voice that
latched a false one runs the render loop that predates the matrix, expression for
expression — the same mechanism `OscSettings::lfoActive` already uses, and for the same
reason, which is that four test files pin this engine sample for sample. The two loops
agree to one rounding and not to the bit: the plain one fuses its multiply-add, and the FM
one cannot, because it needs the product on its own to feed the modulator. That is why the
guarantee is "an untouched matrix never reaches the second loop" rather than "the two
loops agree".

Three things about the DSP that are not obvious from reading it:

- **A modulator is read one sample late.** A matrix with a diagonal has no evaluation
  order that could read the current sample, and the diagonal is what makes feedback free.
  It is what every FM synth with feedback has always done, and at audio rates it is a
  phase error of a fraction of a degree.
- **What a slot sends carries its own gain and the amplitude envelope**, so a patch
  brightens as it is struck and dulls as it decays — the only timbral movement available
  with one envelope per voice. It carries neither velocity nor an LFO's volume swing:
  those say how loud the result is, and folding them in would make a knob marked VOL
  change the timbre of everything downstream of it.
- **The output column is its own parameter, not the level knob.** A level says how loud an
  oscillator IS, which is also what it sends as a modulator; the output column says how
  much of it reaches the speakers. Keeping them apart is what lets a slot be a modulator
  and nothing else.

The band limiting is computed for the unmodulated increment, so a modulated saw or square
aliases. That is the trade rather than a defect: correcting it means a new PolyBLEP per
sample against a discontinuity the modulator has just moved. Sine is the waveform FM is
for, and the other three are still there.

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
- A newer `formatVersion` is refused outright instead of half-read. v19 is current: it
  gave every oscillator slot its row of the FM matrix, additively, with amounts
  defaulting to zero and the output to one so an earlier file loads sounding exactly as
  it did. Unlike v18's `lfo`, these are PROPERTIES of the OSC node rather than a node of
  their own, and the schema omits nodes and not keys - so every committed example and
  preset gained four keys per slot and was regenerated in the same commit.

Saving writes to a temporary and swaps, so an interrupted save cannot destroy the project
it was overwriting.

Audio is the one thing the file cannot hold. A project that references recordings gets a
sidecar folder beside it — `Song.dew` and `Song Assets/` — and stores paths relative to
itself, so the pair can be copied elsewhere intact. Saving gathers: every referenced file
not already in the sidecar is copied in and its path rewritten. A sample imported from
elsewhere on the disk keeps its absolute path rather than becoming a chain of `../`,
which would be portable to nothing.

