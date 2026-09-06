# Testing

Catch2 v3, `catch_discover_tests`, run through CTest. Every gesture is driven headlessly:
there is no window, no message loop and no mouse source, and that is what makes most of
the traps below possible.

```sh
ctest --preset release          # the suite
./scripts/check.sh              # the gate — see workflow.md
```

## `release` is not the gate

**Only `ci` builds warnings-as-errors**, so a whole class of defect passes `release` and
fails `ci`:

- **`CHECK (x == 0.0f)` fails under `-Wfloat-equal`**, fired from inside Catch2's
  decomposer. Use `juce::exactlyEqual`, compare integer counts, or a local `same()` helper
  in a JUCE-free test (`dew_lang_tests` has no JUCE).
- **`-Wswitch-enum` demands every enumerator** even when a `default:` is present.
- A dropped `[[nodiscard]]` result is an error here and a warning there.

`INFO (cond ? "a" : "b")` does not compile: Catch2 expands `INFO` to `MessageBuilder <<
msg` and `<<` binds tighter than `?:`. Wrap it — `INFO ((cond ? "a" : "b"))`.

## Four ways a headless test passes while proving nothing

- **`EditorState` is a `ChangeBroadcaster`, so its callbacks are async.** With no message
  loop they never run, and every assertion after a `setSelected*` holds for the wrong
  reason. Pump with `state.dispatchPendingMessages()` — `runDispatchLoopUntil` is
  unavailable, `JUCE_MODAL_LOOPS_PERMITTED` is off — and **always assert a control case**
  that proves the pump worked.
- **`MouseEvent::mouseWasDraggedSinceMouseDown()` asks the mouse SOURCE**, which no
  synthetic event ever pressed. It is the **last** `MouseEvent` constructor argument;
  `RollHarness::eventAt` and `PlaylistTests::eventAt` take it as a trailing `wasDragged`
  parameter. `getDistanceFromDragStart()` is always 0 for the same reason.
- **`Component::findChildWithID` is NOT recursive.** Rows inside a `Viewport`, or headers
  moved one level down into a clipping holder, break this silently. Write a
  `findDescendantWithID` walk — there is one in `tests/SelectionTests.cpp`.
- **`PopupMenu::MenuItemIterator` keeps a REFERENCE.** Iterating a menu returned by value
  walks a destroyed object and yields an empty list. Bind it to a named local first.

Two more that cost a round each:

- **A `juce::TabbedComponent` parents only the CURRENT tab's content.** A walk of a freshly
  built `MainComponent` covers one tab. Loop `showTab (i)` + `resized()` over
  `Settings::numTabs`, and put the real number in the control case so the gate cannot
  silently shrink back. `tests/ControlWalkHarness.h` does this once —
  `forEachControl (component, fn)` returns the count for the control case. Use it rather
  than writing a fourth copy.
- **`grabKeyboardFocus` does nothing without a `ComponentPeer`**, and this harness has
  none, so `hasKeyboardFocus` is always false. A paint that depends on focus has to take
  it as an argument to be testable at all.
- **`PaintProbe::coverageOf (image, colour::accent)` measures the KNOBS** — every rotary
  arc in dew is drawn in the accent colour. For "did this get drawn", count pixels with
  `getPixelAt (x, y).getAlpha() > 0` (the probe image starts transparent) and pair it with
  a **blank control**: a region of the same component that nothing paints must read zero.

Menus are built as `buildMenu()` + `applyMenuChoice (int)` pairs precisely because
`showMenuAsync` cannot run headlessly; components expose those as a public test seam
(`src/ui/MenuSeam.h`).

## The source gates

`tests/SourceScan.h` + `SourceGate*Tests.cpp`. `offenders(predicate, exempt)` runs the
predicate over every **code** line under `src/` — comments and strings are stripped, so a
predicate says what it looks for and never how a comment is spelled — and reports
`ui/File.cpp:12  the line`.

- **An exemption is a PATH relative to `src/`**, matching that file or anything under that
  directory: `"ui/design/Tokens.h"`, `"model/edits"`. Never a bare name. A name is not a
  property a file keeps when it is split, and five gates went red proving that.
- **An exemption that suppresses nothing FAILS the gate.** So when one goes red, the two
  honest fixes are to change the code or to argue the rule down — never to add a name.
  If a gate goes red at a definition site that moved, the entry that went stale is named
  in the same report.
- **Prefer a predicate that recognises the sanctioned CALL** over a list of sanctioned
  files. `ProjectEdits::setProperty` and `gesture::dragPixelsFor` are both allowed by what
  the line says, so neither needs an entry.
- **Ask a shape, not a spelling.** The undo gate matched the literal `&undo` and therefore
  matched nothing in the tree for as long as it existed — green because it could not see.
- A gate that scans must assert it scanned something. `REQUIRE (files.size() > n)` — a
  gate over an empty walk passes silently, which is the failure mode all of these exist
  to avoid.

## Looking at it

`dew_shot` paints the UI into a PNG with no window. Use it — both of the layout defects
reported by hand were invisible in code and obvious in one render.

```sh
dew_shot editor out.png --project examples/melody.dew --tab piano-roll
dew_shot tabs out            # one PNG per tab
dew_shot gallery out.png     # the design system
```

## Known

Nothing, currently. `./scripts/check.sh` is expected to be green.

This section used to say that `"a reader never accepts a window the writer overtook"`
(`tests/SignalTapTests.cpp`) failed four runs in five and aborted. **That note was stale
and is retired.** It was written at 05:39 on 2026-09-04; `5d4f202`, which gave the two
stress tests a budget in WORK rather than wall-clock, landed at 15:29 the same day and
nobody came back to it. Measured since: 40 isolated runs and three full `ctest --parallel
4` runs, all green.

A note like that is expensive twice over — it tells people to ignore a red gate, and it
says which test to ignore it for. If a gate is genuinely flaky, fix it or hide it behind
`[.]`; do not write down that it fails.

The abort was real, and is fixed. A failing `REQUIRE` in that test **threw past** the two
lines that stopped and joined the writer, so `~thread` on a still-joinable thread called
`std::terminate`: the one test in the suite that could legitimately fail was the one test
that could not report it, taking the whole `ctest` process down instead. The writer is
stopped by a scope guard now, so a tear is reported as a failure naming the window.

`asan`, `tsan` and `scripts/linux-check.sh` run outside the gate and are CI-only; neither
sanitizer runtime works on this macOS. See [workflow.md](workflow.md).
