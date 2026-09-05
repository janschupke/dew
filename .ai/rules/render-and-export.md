# Rendering and export

`src/io/OfflineRenderer.*`, `src/io/RenderJob.*`, `src/io/MidiExporter.*`,
`src/engine/RenderPost.*`, `src/ui/RenderPanel.*`, and the `dew_render` tool. Decisions
that look wrong until you know why.

- **A bar range renders from sample 0 and DISCARDS the head.** `AudioEngine` has a locate
  and using it would be wrong: `Sequencer::collect` emits a trigger only when a step
  boundary falls inside the block and the trigger carries the note's whole duration, so a
  note that began before the in-point would not exist; a seek resets every voice on
  purpose; and effect units are never reset by a seek, so reverb and delay would start
  empty. No pre-roll fixes it in general — a long delay at high feedback rings for minutes.
  The guard asserts a range is **sample-identical** to that window of a full render, which
  a locate cannot produce.
- **Stems MUTE the other tracks**, which is now the only way there is: a track has one
  state. It was already the right answer while there was a solo beside it - stems exist to
  hand back every track, so honouring a solo would have given one file and silence - and
  clearing the mixer-wide flag was part of building a stem. One `AudioEngine` is reused across
  passes with `prepare()` between them — a fresh one churns the whole preallocated effect
  pool per stem. Stems do **not** sum back to the mix when the master chain holds a
  non-linear effect; that is warned about, not fixed.
- **`LAMEEncoderAudioFormat` encodes in its writer's DESTRUCTOR**, blocking on a child
  process, and returns void. So the writer must be destroyed inside its own scope brace
  before the file is checked, the size check is **mandatory** rather than defensive, and
  cancel cannot be honoured once encoding starts. It publishes sample rates and bit depths
  and enforces neither, and an out-of-range quality index silently becomes `lame -b 0` —
  validate rate, depth and quality up front.
- **JUCE has no dither anywhere.** `AudioFormatWriter` rounds and clips. dew's TPDF dither
  is hand-written in `RenderPost`, applied **only at the writer** (a float buffer has no
  LSB), with a fixed seed so renders stay reproducible.
- **Post-processing order is fades → normalize → dither.** Normalize last-but-one so the
  *file* peaks at the target; the other order leaves it quiet whenever the peak sat in a
  fade.
- **MIDI ticks-per-quarter-note is DERIVED, not 960.** `stepsPerBeat` runs to 16 and 960
  does not divide 7, 9, 11, 13 or 14, while `MidiFile` stores the format in a `short`.
  `tempoMetaEvent` takes microseconds per quarter, not BPM. Channel 10 is skipped (GM
  percussion). Velocity clamps to ≥1, because 0 *is* a note-off. The parity test drives
  `Sequencer::collect` over the same span and requires the exporter to produce the same
  notes; that is what stops the two drifting.
- **`dew_render` exits non-zero on a silent render**, because "loaded but made no sound" is
  the failure it exists to catch. A new clip kind that `EngineSnapshot::isSilent` does not
  know about will make it lie — see [architecture.md](architecture.md).
- `OfflineRenderer` builds its **own** `EngineSnapshot`. Anything `buildSnapshot` needs
  must be threaded through `RenderOptions`, or every render and stem export drops it
  silently.

UI: the playlist ruler **scrubs** on a plain drag, so a bar-range selection is a
**shift-drag** (shift-click clears). The range lives in `EditorState`, never in the
ValueTree. `dew_shot render` renders the dialog.
