# Automation

Format version 11. `src/model/AutomationCurve.*`, `src/model/AutomationTargets.*`,
`src/engine/TempoMap.*`, `src/ui/AutomationLane.*`, `src/ui/ParamContextMenu.*`.

- **`curveValueAt` is THE evaluator.** `ProjectEdits::automationValueAt`,
  `AutomationSnapshot::valueAt` and the playlist's painter all call it. They were once two
  hand-copied bodies in two layers and a painter that implemented neither — it drew a
  chord, so the per-point `curve` bend was heard and never seen. A test compares editor and
  engine at 201 steps. Do not write a second evaluator.
- **Two segment shapes stored, three offered.** `SegmentShape { curve, step }`; "Line" is
  `setPointStraight`, which is shape `curve` with bend 0, because a stored `line` would be a
  second place holding the same fact as `curve == 0`. A stepped segment **ignores** the
  bend rather than flattening it, so the menu is reversible.
- **`automationTargetFor (project, node, property)` is the primitive**;
  `availableAutomationTargets` is a walk over it, and both directions are asserted equal. A
  control right-clicks by asking this — it is why automation is reachable from a knob.
- **`ParamSpec::automatable` is the only gate.** An `AutomationTarget` carries a `const
  ParamSpec*`; `paramMenu::Context` stores the spec **by value** because `effectParamsFor`
  returns its table by value and a pointer would dangle. A gate refuses an automatable
  parameter spelled as a string literal.
- **Frequency-like parameters map exponentially** (cutoff, delay time, rate, mid
  frequency). Linear mapping puts four fifths of a drawn curve above 3kHz.
- **`automationValueFor` snaps a discrete parameter; `ParamSpec::fromNormalised` does
  not** — the latter is what a knob reads and must stay continuous.
- **`TempoMap::isConstant()` is a CORRECTNESS requirement, not an optimisation.** The
  render tests compare with `juce::exactlyEqual`, and `(60/bpm*sr/spb)*steps` differs in the
  last bit from `(60/bpm/spb)*steps*sr`. A project with no tempo curve must take the
  arithmetic it always took.
- The map lives in `EngineSnapshot` and defaults to non-null, which is why no
  `RenderOptions` field is needed: `OfflineRenderer`, `RenderPanel` and `MidiExporter` each
  build their own snapshot. **A hand-built `EngineSnapshot` in a test must set a map that
  agrees with its `tempoBpm`**, or every clip after bar 0 is misplaced and only the
  rendering tests notice.
- **`SamplePlayer` takes a sample position AND the map.** A clip's placement follows the
  curve; its playback rate must not — you do not time-stretch a recording because somebody
  drew a tempo ramp.

**Deliberately not automatable, each for its own reason.** Do not "fix" one of these:

| Not automatable | Why |
| --- | --- |
| playlist-track mute | A track carries no id, and it is circular — `collectAutomation` skips clips on inaudible tracks, so a curve muting its own track could never un-mute |
| `sample.reverse`, `sample.loop` | A read-pointer discontinuity |
| pattern length | Nothing for a curve to mean |
