# Architecture

Seven static libraries. **There is no `dew_core`** — anything that says so is out of date.
The layering is a link error, which is the whole reason they are libraries rather than
directories; see [README.md](../../README.md#architecture) for the argument.

| Library | Directory | May depend on |
| --- | --- | --- |
| `dew_lang` | `src/lang/` | **nothing at all**, JUCE included |
| `dew_model` | `src/model/` | JUCE core only. A leaf: nothing of dew's |
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
- **Adding a library** means adding it to the `DEW_GATE_SOURCES` foreach in
  `tests/CMakeLists.txt` *and* to the layer list in the gate "every layer is represented
  in the scanned sources". Miss either and the gates silently stop covering it.

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
