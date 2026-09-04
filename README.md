# dew

A desktop digital synth DAW in the FL Studio shape: a channel rack with a step grid, a
piano roll, a playlist of clips and a mixer, driven by a three-oscillator synth per
channel — or, on an audio channel, by a recording.

Status: **working prototype**. New, open, edit, save and playback work end to end, with
effects and automation on top. It is not a product, but every layer is real and wired to
the next. macOS is where it is developed and where the gate runs; Windows and Linux build
the whole application and run the whole suite in CI, on every push.

- **Source** — <https://github.com/janschupke/dew>
- **Download** — <https://github.com/janschupke/dew/releases/latest>
- **The site** — what it does, the design system, and the score language's generated
  reference. Built from `website/`; `/setup/` there is this page's Build section, kept in
  step by a test.

## Download

| Platform | File |
|---|---|
| macOS 11+, Apple Silicon and Intel | [`dew-macos-universal.dmg`](https://github.com/janschupke/dew/releases/latest/download/dew-macos-universal.dmg) |
| Windows 10/11 x64 | [`dew-windows-x64-setup.exe`](https://github.com/janschupke/dew/releases/latest/download/dew-windows-x64-setup.exe) · [`dew-windows-x64.zip`](https://github.com/janschupke/dew/releases/latest/download/dew-windows-x64.zip) |
| Linux x86_64, glibc 2.35+ | [`dew-linux-x86_64.AppImage`](https://github.com/janschupke/dew/releases/latest/download/dew-linux-x86_64.AppImage) · [`dew-linux-x86_64.tar.gz`](https://github.com/janschupke/dew/releases/latest/download/dew-linux-x86_64.tar.gz) |

Those links carry no version and never break: GitHub resolves
`releases/latest/download/<asset>` to the newest release holding that name. Every release
also carries `SHA256SUMS.txt` and a Sigstore build attestation —
`gh attestation verify <file> --repo janschupke/dew` says which workflow and which commit
produced it.

**None of these builds is signed.** macOS will refuse the first launch and send you to
System Settings → Privacy & Security → Open Anyway; Windows will show SmartScreen's
"Windows protected your PC" and hide the button behind More info. A certificate costs
money every year and this is a prototype. The site's `/download/` page says exactly what
each system does, in the words it uses.

**MP3 export is unavailable in a downloaded build.** dew drives an installed `lame` as a
child process rather than shipping an encoder, so the option greys itself out until one is
on `PATH`. See [Dependencies](#dependencies).

Old versions stay on [the releases page](https://github.com/janschupke/dew/releases) for
as long as GitHub keeps them, which is indefinitely.

## Build

```sh
brew bundle                    # cmake >= 3.25, ninja, ccache, lame
cmake --preset release
cmake --build --preset release
```

That is macOS. On Linux the same two CMake commands work once the distribution has a
compiler, CMake 3.25 or newer, Ninja, and the ALSA, X11, freetype and fontconfig
development packages — `.github/workflows/ci.yml` names the exact apt list, because a list
CI runs is a list that is true. On Windows, `cmake --preset dist-windows` builds with the
Visual Studio generator, which finds its own toolchain; the Ninja presets would need a
developer command prompt.

Presets, not raw flags:

| Preset | What it is |
|---|---|
| `dev` | Debug, with tests |
| `release` | RelWithDebInfo — build this for normal use |
| `ci` | `release` plus warnings-as-errors; the gate |
| `asan` | `ci` plus AddressSanitizer and UndefinedBehaviorSanitizer |
| `tsan` | `ci` plus ThreadSanitizer |
| `dist` | What a release is built from — no tests, universal on Apple |
| `dist-windows` | `dist`, built by the Visual Studio generator |
| `offline` | `release` from a warm dependency cache, no network |

`dev` also turns on libc++'s debug hardening, which bounds-checks `operator[]`. It costs
nothing and the render path indexes a snapshot's channels on every block.

The first configure fetches JUCE and Catch2 from source — a few seconds, shallow, cached
per machine under `~/.cache/CPM` — so one build is slow and the rest are not.

## Run

```sh
open build/release/src/dew_artefacts/RelWithDebInfo/dew.app
```

Then **Demos → Getting Started** in the menu bar. There are eight, and each one is the
demo for a different part of the app rather than eight versions of the same song:

| Demo | | What it is there to show |
|---|---|---|
| Getting Started | 16 bars | The step grid, and a playlist that is an arrangement rather than a loop |
| Piano Roll | 32 bars | Chords, held notes, a velocity shape, and an A-B-bridge form |
| Effect Chain | 32 bars | Six of the ten effects, all three chain locations, a four-deep chain, a bypassed slot |
| Automation | 32 bars | Five scopes including the tempo, curve bends, and segments that step |
| Wavetable | 32 bars | The five wavetables, unison, both position sources, a morph drawn as a curve |
| Oscillator Stack | 32 bars | Three oscillators a channel — octaves, cent detune, per-slot gain |
| Song Structure | 32 bars | Three lanes coming and going independently, and four drums on one bus |
| Compiled from a Score | 52 bars | The score language: `examples/amber.score` is what it is |

Three of them get their notes from a `.score` beside them and their sound from
`src/model/Demos.cpp`, which is the language's own division: it owns notes, patterns and
clips, and the instrument, the effects and the mixer are yours. Opening one puts its text
in the Score tab, so **Compile** reproduces exactly what is already playing.

The files under `examples/` are generated by `dew_render --write-demos examples` and
committed, and three tests pin file == binary == factory — so a demo cannot be edited in
one of the three places and left stale in the other two.

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
dew_shot --help                      # and the five dialogs
```

Screen-recording permission is not always available, and a layout defect is obvious in a
picture and nearly invisible in code.

## Test

```sh
ctest --preset release        # 1296 tests
```

The gate, which is what CI runs and what a change has to pass:

```sh
./scripts/check.sh
```

which is the dependency pins, the formatting, a warnings-as-errors build, the
tests and the generated manifest, in the order that fails cheapest first.

`release` is not the gate. Only `ci` builds warnings-as-errors, so it is the only one
that catches an exact float comparison or a dropped result.

Two things run outside it, because both are slow enough that putting them in front of
every change would only teach people to skip the gate:

```sh
ctest --preset asan           # AddressSanitizer and UndefinedBehaviorSanitizer
ctest --preset tsan           # ThreadSanitizer, for the three lock-free queues
./scripts/linux-check.sh      # the score language under gcc and libstdc++, in Docker
```

`tsan` is aimed at `SnapshotBridge`, `PreviewQueue` and `SignalTap`, which are hand-rolled
and where a race would look like a flake rather than a failure. There is no leak check:
`detect_leaks` is unsupported on Apple Silicon, and with no manual `delete` anywhere in
the tree there is little for it to find.

**Both are CI-only at the time of writing.** Neither sanitizer runtime works on macOS 26.4
with Xcode 26.4: `int main(){}` built with `-fsanitize=address` spins forever inside
`__asan::InitializeShadowMemory`, and the same program under `-fsanitize=thread`
segfaults. UBSan is unaffected, which is why it rides along with `asan`. The presets are
correct and the CI runners are an older macOS; they have not been run here.

`linux-check.sh` exists for one claim. The score language writes out its own splitmix64
and PCG32 because libstdc++ and libc++ generate different numbers, and until there was a
second standard library in the loop that was an assertion rather than a test. It is cheap
because `dew_lang` links nothing — JUCE included — so the container needs a compiler and
CMake and no system libraries at all. The same thirteen files build as `dew_lang_tests`,
which is also a faster inner loop for language work than the full suite.

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
  otherwise**. Pitch rows stretch, and past a threshold the key strip names every key
  rather than only the Cs — which is the difference between writing a melody and reading
  a voicing across four octaves.
- **Playlist** — pattern clips on tracks along a bar timeline; a clip longer than its
  pattern repeats it. Drag clips between tracks, double-click to open a pattern, mute or
  solo a lane. Lanes stretch — by the toolbar, by ⌥`=`, or by dragging a header's bottom
  edge — because an automation curve drawn into a 34px lane has a 28px value axis and a
  7px grab radius.
- **Colour** — a channel, a playlist track and a mixer strip each carry one, from an
  eight-entry ramp, on their right-click menu. Leaving it unset means *inherit*, which is
  what all three did before they could choose: a lane took the colour of its position in
  the list and a strip the colours of the channels routed into it.
- **Automation** — clips on the playlist driving a declared set of targets: channel and
  track volume and pan, master gain, and any effect parameter. Drag points, double-click
  to add, alt-click to remove.
- **Mixer** — a fader, pan, mute, solo and a peak meter per insert, plus master; below
  them, the effect chain of the selected strip. Each strip lists the channels routed into
  it, and clicking one goes there. Solo is resolved across the whole mixer. Add an insert
  from the column past the last strip or from any strip's menu, and remove one from its
  own; the channels feeding a removed insert move to the first remaining one, in the same
  undo step. The master is neither renamed nor removed — it is a different node type, so
  that is structural rather than a check.
- **Effects** — reverb, filter, delay, drive, distortion, chorus, phaser, a 3-band EQ,
  a compressor and a limiter, chained up to four
  deep on any channel, mixer track or the master. One editor pointed either way round: an
  accordion down the instrument panel, a row of open cards across the mixer. Drag a card
  by its grip to reorder: it lifts and follows the pointer, the rest of the chain parts to
  open a gap where it would land, and the move happens when you let go — one undo step for
  the whole drag, however far it travelled. Escape abandons it, and so does letting go
  outside the chain.
- **Instrument** — three band-limited oscillators (sine/saw/square/triangle) or
  wavetables with unison, each with its own octave, detune, gain and on/off switch. One
  ADSR envelope behind them, and channel volume and pan.
- **Presets** — thirty-nine factory sounds: three for each effect type, five for the synth,
  two for an audio channel and two for a soundfont. **Preset** beside the instrument panel's title loads one
  onto the selected channel; each effect card has its own button, and offers only its own
  type. Loading one is a single undo step. A preset carries the *sound* and nothing else —
  not a channel's name, colour, routing, level or base pitch, and not its effect chain — so
  loading one in the middle of a mix cannot move a fader or retune a part that is already
  written. They ship as files under `presets/`, embedded in the binary the way the demos
  are; there is no user save yet.
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
- **It remembers** — window geometry, interface scale, active tab, selections, the piano
  roll's zoom, scroll, row height and snap, the playlist's lane height, the score's text
  size, panel width and chosen device. The piano roll's *tool* deliberately
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
pattern/song · ⌘K add channel. ⌘1 – ⌘5 go to the five tabs, ⌃⇥ cycles them, ⌘\ folds the
instrument panel away.

Every key dew binds is a row in `src/ui/Hotkeys.h` and a gate refuses one spelled anywhere
else. The full table — what each editor's keys mean, the tools, the canvas cursor, what a
wheel notch is worth and where shift means *finer* — is in
[`.ai/rules/gestures-and-hotkeys.md`](.ai/rules/gestures-and-hotkeys.md).

## Architecture

Nine layers, each a static library, each testable without the ones above it. Libraries
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
dew_control  ControlOps (every operation an agent can call, declared once)
             McpServer (sessions, consent, grants) · ParamAddress
             Links dew_io and stops there: it cannot paint, and everything
             above it arrives through ControlHost.                 │

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
             Depends on dew_lang and dew_i18n, and nothing else of dew's.

             │ AsyncUpdater coalesces rebuilds ─→ EngineSnapshot
             │ PreviewQueue carries auditioned notes
                                                 │
                                                 ▼
dew_i18n     StringIds (generated) · Catalogs (generated) · MessageFormat · PluralRules
             Every sentence a person reads, by structural key. Links juce_core and
             nothing else, so a catalogue cannot open a file or build a ValueTree.

dew_lang     The score language. Links nothing at all, JUCE included — which is why
             it carries a second catalogue of its own, generated from the same
             en.json into std::string_view rows that need no juce::String.
```

## Design system

`src/ui/design/` holds the vocabulary — colour roles, spacing, type, a size ladder, an
*emphasis* scale, motion durations, and forty-odd icons drawn as `juce::Path` rather than
shipped as assets. `src/ui/primitives/` holds the controls built on it. `dew_shot gallery`
renders the whole set to a PNG, which is how it is reviewed.

Nothing in it is optional: a source-scanning test refuses a hex colour, a bare radius or
gap, a size the ladder already names, a timer picking its own refresh rate, and a token
nothing refers to. Two palettes, both dark, both held to a contrast ratio in either
direction. The rules, the gates and the argument for each are in
[`.ai/rules/design-system.md`](.ai/rules/design-system.md).

## The website

`website/` is a Next.js site: what dew is, what it does, how to build it, and the score
language's generated reference. `./scripts/check-website.sh` checks it and
`./scripts/check.sh` runs that; `vercel.json` at the root is how it deploys.

Four of its inputs — the score schema, the design tokens, the highlighted samples and the
MCP reference — are JSON written by `dew_docs`, `dew_shot` and `dew_mcp` and committed, so
the site needs no C++ toolchain to build and CI regenerates each in a second process and
compares byte for byte. The rest, including why it is dark-only and what it may not do, is
in [`.ai/rules/website.md`](.ai/rules/website.md).

## Driving it from an agent

dew runs a Model Context Protocol server inside the application, so a coding agent can
read and change the project you have open — instruments, effects, the mixer, the notes and
the arrangement. Turn it on under **Audio > MCP**, which also shows the address and the
exact command:

```sh
claude mcp add --transport http dew http://127.0.0.1:4551/mcp
```

It listens on the loopback interface only and validates the `Origin` header, so nothing
off your machine can reach it and a page in your browser cannot drive it either. The first
time a client connects, dew names it and asks whether to allow it — and whether it may only
read, or change things too. **Every call that changes the project is exactly one undo
step**, however many notes it wrote, which is what makes the approval a reasonable thing to
give.

Every operation is declared once, in `src/control/ControlOps.h`, and three things read that
one table: the protocol builds each tool's schema from it, the permission check reads
whether an operation writes, and `dew_mcp` walks it into the website's reference — so a
tool cannot exist without being documented. Five fields address every value in the
document, and `score_compile` is how an agent writes a lot of music at once rather than a
note at a time. [`.ai/rules/mcp.md`](.ai/rules/mcp.md) has the whole argument.

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

## Where the rest of it is written down

This file is the tour. The detail lives beside the thing it constrains, in
[`.ai/rules/`](.ai/rules/) — one file per subject, each stating what you must do before
writing code and then, under *Why it is this way*, the argument for it.

| | |
|---|---|
| [Architecture](.ai/rules/architecture.md) | the eight libraries, modules, the project file |
| [The audio thread](.ai/rules/realtime.md) | what the render path may not do, and the three queues |
| [Design system](.ai/rules/design-system.md) | tokens, the gates, the two palettes, focus, motion |
| [Gestures and hotkeys](.ai/rules/gestures-and-hotkeys.md) | every key, the wheel, the canvas cursor |
| [Testing](.ai/rules/testing.md) | why `release` is not the gate, and how a headless test lies |
| [The score language](.ai/rules/score-language.md) | determinism, counterpoint, the editor, recompiling |
| [Rendering and export](.ai/rules/render-and-export.md) | bar ranges, stems, LAME, the post-processing order |
| [Automation](.ai/rules/automation.md) | one evaluator, and what is deliberately not automatable |
| [Strings](.ai/rules/i18n.md) | structural keys, the two generated catalogues, what a file's names are, adding a language |
| [C++ style](.ai/rules/cpp-style.md) | `.clang-format`, includes, the `juce::String` UTF-8 trap |
| [The website](.ai/rules/website.md) | its own gates, its generated JSON, what it may not do |
| [Workflow](.ai/rules/workflow.md) | the gate, the generated files, commit style |
| [Releasing](.ai/rules/release.md) | the two lines a version lives in, the tag check, packaging, signing |

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

**[AGPLv3](LICENSE)**, in full, in the repository root — not only as this sentence. JUCE 9
is dual-licensed commercial or AGPLv3 and dew takes the AGPL terms, which oblige an offer
of the corresponding source to whoever receives a binary. Somebody holding a `.dmg` has
never read this file, so both licence texts travel inside the package: `dew-LICENSE.txt`,
`JUCE-LICENSE.md` and `THIRD_PARTY.md` are in the app bundle's `Resources` on macOS and
beside the binary elsewhere. See [THIRD_PARTY.md](THIRD_PARTY.md).
