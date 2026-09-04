# dew

A macOS-only desktop synth DAW in the FL Studio shape: a channel rack with a step grid, a
piano roll, a playlist of clips and a mixer, driven by a three-oscillator synth per channel
— or by a recording, or by a song written as text and compiled to notes. C++20, JUCE 9,
CMake, Catch2. Seven static libraries rather than seven directories, so a layering mistake
is a link error.

[README.md](README.md) is the tour and the reasoning: what the app does, why each decision
was taken, and what was rejected. All detailed rules live in [`.ai/rules/`](.ai/rules/) —
the single source of truth for what you must do before writing code.
[`.ai/rules/README.md`](.ai/rules/README.md) states the rule about the rules: one fact, one
home, and every other mention is a sentence and a link. Read it before adding to any of
these files.

## Rules

- [Architecture](.ai/rules/architecture.md) — the seven libraries and what each may depend on, rooted includes, one `ParamSpec` per parameter, every edit through `ProjectEdits`, and the two things that fail silently (a new clip kind, a metre that is not a tempo)
- [The audio thread](.ai/rules/realtime.md) — the render path allocates nothing, effect modules are never destroyed, no command queue, and the three queues with three different contracts
- [Design system](.ai/rules/design-system.md) — everything from `Tokens.h`; no hex colour, no bare radius or gap, no size the ladder already names, no orphan token, no colour pair below its contrast ratio in EITHER palette; icons as `juce::Path`; motion off unless the application turns it on; a control reachable by tab, ringed when focused, and named for a screen reader
- [Gestures and hotkeys](.ai/rules/gestures-and-hotkeys.md) — every key a row in `Hotkeys.h`, `matches()` ignores shift, a wheel notch is pixels divided at the point of use, and view geometry lives on the view
- [Testing](.ai/rules/testing.md) — `release` is not the gate, `juce::exactlyEqual` not `==`, and the four ways a headless test passes while proving nothing
- [The score language](.ai/rules/score-language.md) — `dew_lang` links nothing, hand-written RNG keyed on the structural path, byte-identical **across processes**, the three host walls, and what is settled
- [Rendering and export](.ai/rules/render-and-export.md) — a bar range renders from sample 0, stems mute rather than solo, LAME encodes in its writer's destructor, and the post-processing order
- [Automation](.ai/rules/automation.md) — one evaluator, `automationTargetFor` as the primitive, `ParamSpec::automatable` as the only gate, `TempoMap::isConstant` as correctness, and what is deliberately not automatable
- [C++ style](.ai/rules/cpp-style.md) — `.clang-format` is the authority; hand-grouped includes, the `juce::String` ASCII/UTF-8 trap, and the standard-library limits on this deployment target
- [Workflow](.ai/rules/workflow.md) — `./scripts/check.sh`, the generated files, the dependency pins, commit style, and why `build/ci` is not the app

## Commands

`/plan` · `/audit` · `/audit-scatter` · `/audit-security` · `/bugfix` · `/refactor` are
registered as user-level skills and apply here unchanged. There is no `.ai/commands/` in
this repo.

## Critical rules (excerpt)

Full set in [`.ai/rules/`](.ai/rules/). The ones an agent trips over first:

- **`./scripts/check.sh` is the gate** — pins, the generated Cursor rule, formatting, a
  warnings-as-errors build, the tests, the generated manifest. Run it before saying
  anything is done. **`ctest --preset release` is not the gate**: only `ci` builds
  warnings-as-errors, so `CHECK (x == 0.0f)` passes `release` and fails `ci` from inside
  Catch2's decomposer. Use `juce::exactlyEqual`.
- **Read `tests/SourceGateTests.cpp` before writing any UI file.** Every dimension, colour,
  duration and gesture comes from `src/ui/design/Tokens.h` and `src/ui/design/Gestures.h`:
  no hex colour, no bare emphasis, radius, stroke, gap or inset, no size the ladder already
  names, no timer picking its own refresh rate — and no token nothing refers to.
- **Every key dew binds is a row in `src/ui/Hotkeys.h`**, and a gate refuses one spelled
  anywhere else.
- **Every edit goes through `ProjectEdits`**, one undo step per gesture, and **never
  hand-build a `ValueTree` node** — use `defaultTreeFor (spec)`.
- **A parameter is declared once**, as a `ParamSpec` row in `src/model/ModuleCatalog.cpp`.
  The schema defaults, engine clamps, automation range and UI control are views of it.
- **The render path allocates nothing**, and an effect module is **never destroyed while
  the engine lives** — `SnapshotBridge` has no acknowledgement path, so anything a snapshot
  points at must outlive every snapshot.
- **`dew_lang` links nothing at all**, JUCE included, and never uses `std::shuffle`,
  `std::uniform_int_distribution` or `juce::Random` — libstdc++ and libc++ would render
  different music.
- **`THIRD_PARTY.md`, `examples/`, `presets/` and `.cursor/rules/main.mdc` are generated.**
  Never hand-edit one; regenerate it in the same commit.
- **`build/ci` is not the app.** The user opens
  `build/release/src/dew_artefacts/RelWithDebInfo/dew.app`, so after a UI change also run
  `cmake --build --preset release`, and verify it with `dew_shot` rather than by reasoning.
- **Never create a branch.** Commit to the branch you are already on, with an imperative
  sentence-case subject and no prefix.
