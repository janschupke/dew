# Gestures and hotkeys

Two files own this, and a gate holds each: `src/ui/Hotkeys.h` for the keyboard,
`src/ui/design/Gestures.h` for the mouse. `Gestures.h` is mouse-only and header-only;
`gesture::Command` is gone and is now `hotkeys::ViewCommand`.

## Keys

- **Every key dew binds is a row in `src/ui/Hotkeys.h`.** The gate "no source binds a key
  outside the hotkey registry" refuses `addDefaultKeypress`, `juce::KeyPress (` or
  `createFromDescription` anywhere but `Hotkeys.h`/`Hotkeys.cpp`. There is exactly one
  exemption, `ScoreEditorComponent.cpp`: while its completion popup is open it owns Up,
  Down, Return, Tab and Escape, and nothing outside that popup can reach them, so that is
  a modal handler rather than a binding.
- `hotkeys::application()` drives `getCommandInfo` and the menu bar;
  `hotkeys::viewCommandFor` answers what a key means in a timeline view.
- **`matches()` compares command/ctrl/alt EXACTLY and ignores shift**, because `+` and `_`
  are how a keyboard spells shift-`=` and shift-`-`. So the "other size" trio is ⌥`=` ⌥`-`
  ⌥`0`, not shift-anything, and a ⌘-digit binding in `timeline()` would break the
  deliberate regression guard "a modified digit is not a timeline command".
- **Adding a `ViewCommand` is a compile error** in the step grid, the piano roll and the
  playlist until each answers it. That is the mechanism; use it rather than a default case.
- A row may carry `Stroke{}` and no key: `describe` skips `addDefaultKeypress` for keyCode
  0, and `HotkeyTests`' `sameStroke` treats two keyless rows as non-colliding.

## The mouse

- **`gesture::wheelPixelsPerNotch` is in PIXELS, and every view divides into its own unit
  at the point of use.** A notch used to mean six different things across six views. The
  gate "no view reads the wheel or the drag scale for itself" refuses a view that reaches
  past the shared decision.
- `gesture::intentOf` returns the four-way `WheelIntent`; a cross-zoom also satisfies
  `isZoom`, so **order matters** — that is why the decision is shared and the four
  outcomes are not. The step grid is deliberately not a caller: one axis.
- `gesture::deltaOf` applies the system's natural-scrolling flag. JUCE *reports*
  `isReversed` rather than applying it, so a handler that reads `deltaY` directly scrolls
  backwards for anyone on the Mac default.
- **Shift means finer wherever a drag changes a VALUE**, and nothing else. Shift already
  means suspend snap, extend a selection, make a copy unique and transpose by an octave —
  every one of which changes a selection or a position, not a value.
- `getDistanceFromDragStart()` is always zero in a headless harness, so a component that
  wants its drag tested keeps its own origin and calls `gesture::passedThreshold`.

## Where view state lives

**View geometry lives on the view.** The playlist's lane height is on
`PlaylistComponent`, the roll's scroll offset is `PianoRollComponent::pitchScrollPx` — not
in the document (it is not part of the music, must not dirty a project and must not land
on the undo stack) and not in `EditorState` (a `ChangeBroadcaster` whose other listeners
have no interest in it, and which drags `dispatchPendingMessages()` into every new test).
A project load therefore does not reset it, which is asserted.

**UI scale multiplies the peer** — `juce::Desktop::setGlobalScaleFactor`, from
`DewApplication::applyUiScale` — never the type scale. dew's layout is a ladder of pixel
sizes a font must fit inside, so scaling text alone clips a caption in the box it was
measured for. It reaches nothing offscreen, so every `dew_shot` render and headless test
stays at 1:1 and no baseline moves.

## The canvas cursor

The piano roll, the playlist and the step grid each hold a `CanvasCursor`
(`src/ui/CanvasCursor.h`) — where the arrow keys are on a surface that paints its contents.

- **It is a coordinate, never a `juce::ValueTree`.** A position survives an edit and an
  undo; a tree does not, and `ProjectEdits::moveClipToTrack` detaches the one it was given.
- **It is not the selection.** The cursor is where you are; the selection is what you have
  chosen. The step grid has no selection and still has a cursor.
- **What the two axes mean is the view's business** — step and pitch, bar and track, step
  and channel. `CanvasCursor` only knows how to be somewhere and how to stay in bounds.
- **`paint::cursorOutline` takes a bool**, for the same reason `paint::focusRing` does:
  `grabKeyboardFocus` does nothing without a `ComponentPeer` and the harness has none.
- Each view needs a `boundsForCell`-shaped function, because the cursor sits on a place
  that may hold nothing. All three have one now, beside their `boundsForNote` /
  `boundsForClip`.
- **`ControlWalkHarness` cannot see any of this.** It finds controls by type, and a cursor
  is not a component; cursor tests live in `tests/CanvasCursorTests.cpp` and drive the
  three existing view harnesses.
