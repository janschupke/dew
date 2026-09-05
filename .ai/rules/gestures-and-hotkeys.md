# Gestures and hotkeys

Two files own this, and a gate holds each: `src/ui/Hotkeys.h` for the keyboard,
`src/ui/design/Gestures.h` for the mouse. `Gestures.h` is mouse-only and header-only;
`gesture::Command` is gone and is now `hotkeys::ViewCommand`.

## Keys

- **Every key dew binds is a row in the registry**, which is two files and one idea.
  `src/ui/design/Keys.h` (dew_design) says what a stroke IS — `keys::Stroke`,
  `keys::Binding`, `matches`, `keyPressFor` — and declares the one table whose controls
  live in that layer, `keys::valueKeys`. `src/ui/Hotkeys.h` (dew_ui) declares the
  application's rows and re-exports the value table as `hotkeys::value()`, so the
  collision walk still covers one registry.
- **The mechanism is down there because a knob cannot see dew_ui.** Only the mechanism
  moved: `hotkeys::application()` names `CommandIDs::compileScore`, `addChannel` and
  `fileRender`, and dew_design's own CMakeLists says it "knows nothing about a project".
- The gate "no source binds a key outside the hotkey registry" refuses
  `addDefaultKeypress`, `juce::KeyPress (` or `createFromDescription` anywhere but
  `Hotkeys.cpp`, `design/Keys.h` and `ScoreEditorComponent.cpp` — the last because while
  its completion popup is open it owns Up, Down, Return, Tab and Escape, and nothing
  outside that popup can reach them, so that is a modal handler rather than a binding.
- `hotkeys::application()` drives `getCommandInfo` and the menu bar;
  `hotkeys::viewCommandFor` answers what a key means in a timeline view;
  `keys::valueKeys::commandFor` what it means to a knob, fader or number field.
- **`matches()` compares command/ctrl/alt EXACTLY and ignores shift**, because `+` and `_`
  are how a keyboard spells shift-`=` and shift-`-`. So the "other size" trio is ⌥`=` ⌥`-`
  ⌥`0`, not shift-anything, and a ⌘-digit binding in `timeline()` would break the
  deliberate regression guard "a modified digit is not a timeline command".
- **Adding a `ViewCommand` is a compile error** in the step grid, the piano roll and the
  playlist until each answers it. That is the mechanism; use it rather than a default case.
- A row may carry `Stroke{}` and no key: `describe` skips `addDefaultKeypress` for keyCode
  0, and `HotkeyTests`' `sameStroke` treats two keyless rows as non-colliding. `viewNextTab`
  and `viewPreviousTab` are two of them since ⌃⇥ became the group ring's key.

### Changing a value from the keyboard

`DewSlider` and `DewNumberField` answer these, and `juce::Slider`'s own handler is never
called — it stepped by `getInterval()`, which the catalog sets to `0.001` on volume, pan,
sustain, release and gain, and it refused every key with a modifier down, so shift did not
refine the step but blocked the edit.

| | moves |
|---|---|
| ← ↓ / → ↑ | 1% of the range, **in normalised space** — a hundred presses end to end |
| ⇧ + an arrow | one `interval`: the finest legal value the control has |
| `page up` / `page down` | 10% — ten presses end to end |
| `home` / `end` | nothing, deliberately. `home` is Rewind and must stay reachable |

Two things follow from "normalised space". A logarithmic control steps by ratio rather than
by span, so a press means the same musical distance wherever the knob is standing; and the
step is read off the SLIDER's own `NormalisableRange`, not off the `ParamSpec`, because a
range skew is a power curve while `ParamSpec::fromNormalised` is a true exponential. Each
control is then wrong in the same direction as its own drag, which is the pair a hand can
feel.

A discrete or integral parameter still moves a whole unit: 1% of a 0–7 stepper is 0.07 and
the snap would put it straight back, which is an arrow key that silently does nothing.

**Shift is a variant, not a second binding** — read off the `KeyPress` at the call site, the
way the piano roll's ⌥⇧ transpose already is. That is what lets `matches()` go on ignoring
shift.

### Reaching a group

**⌃⇥ and ⌃⇧⇥ step over a whole component** — an effect card, a mixer strip, a rack row, a
panel — where ⇥ steps one control. There are 264 controls across the five tabs, so ⇥ alone
put the third effect card's cutoff dozens of presses from the transport bar.

That key was the only mod-⇥ free on all three platforms dew ships: ⌘⇥ is the macOS
application switcher, ⌥⇥ is the window switcher on Windows and on every mainstream Linux
desktop, and ⌃⌥⇥ is Windows' persistent one. It cost the editor-tab cycling that used to
hold it — ⌘1–⌘5 and the View menu still switch tabs.

- **A group is a component that declares `FocusContainerType::focusContainer` and has a
  `setTitle`.** The flag was already there on twelve panels and shaped only the screen
  reader's tree; `focusGroups` is what makes the keyboard read the same list.
- **`isFocusContainer()` alone is NOT the test.** `juce::Label::setEditable` makes an
  editable label a `keyboardFocusContainer`, and so are `ScrollBar`, `TabbedButtonBar` and
  `PropertyPanel`'s viewport — and `keyboardFocusContainer` implies `focusContainer`. dew
  declares the plain one and never the keyboard one (a keyboard container confines ⇥ with
  no key to leave it, which is a trap), so that choice is also what tells dew's groups from
  JUCE's.
- **`MainComponent` must never become a focus container.** `findFocusContainer()` walks up
  and returns the top-level component whether or not it is one, so making the window a
  container would make the gate "every control in the window belongs to a group"
  unfailable.
- The decision is `focusGroups::nextFocusFor`, which takes the focused component as a
  PARAMETER. `grabKeyboardFocus` is inert without a peer, so everything testable has to sit
  above the one call that is not.

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
- `gesture::dragPixelsFor(mods)` applies that rule to `dragPixelsForFullRange`. Hand it to
  `setMouseDragSensitivity` rather than combining the two by hand: the gate allows a call
  that NAMES `gesture::` and refuses one that picks its own number, so this is the form
  that needs no exemption.
- **A rotary answers VERTICAL travel and nothing else** — `juce::Slider::RotaryVerticalDrag`,
  not JUCE's default `RotaryHorizontalVerticalDrag`, which adds the two axes together and so
  lets a hand's sideways drift cancel its own pull. "A knob answers vertical travel, and only
  vertical travel" in `tests/DesignSystemTests.cpp` is the guard, and it is the only test that
  drags a knob and reads the value back.
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

## Why it is this way

⌘N ⌘O ⌘S ⇧⌘S · ⌘E render · ⌘R compile score · ⌘Z ⇧⌘Z · Space play · R record · ⌘L
pattern/song · ⌘K add channel · ⌘, preferences.

⌘, is the settings window on every platform, so it is the one stroke this table would be
wrong to spell any other way. It used to open Audio Settings, which now has no key of its
own: the menu item is still there and Preferences shows the very same panel.

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

| | step grid | piano roll | playlist | score |
|---|---|---|---|---|
| `+` `-` `0` zoom | ✓ | ✓ | ✓ | — |
| ⌥`+` ⌥`-` ⌥`0` the other size | — | pitch rows | lanes | text |
| `1` `2` `3` tools | — | select · paint · slice | select · paint | — |
| `esc` clear selection | — | ✓ | ✓ | — |
| `del` delete selection | — | ✓ | — | — |
| ⌘A select all | — | ✓ | — | — |
| ← → ↑ ↓ move the cursor | ✓ | ✓ | ✓ | — |
| `return` act on the cursor | toggle a step | toggle a note | open the clip | — |
| ⌥↑ ⌥↓ transpose | — | ±1, ⌥⇧ for ±12 | — | — |

**Tab reaches every control**, and the one holding the keyboard draws an accent ring.
Neither used to be true: knobs refused focus because `juce::Slider` does, and the toolbars
refused it deliberately to keep a click from moving focus off the editor. Both are gates
now — see [Reaching it without a
mouse](design-system.md#reaching-it-without-a-mouse).

**The three timelines have a cursor.** They paint their notes, clips and cells rather than
parenting them, so until it existed there was nothing for a keyboard to land on: three of
the five tabs could only be edited with a mouse, and a screen reader met a rectangle with a
name and no contents. The arrows move it, `return` acts on what is under it, and it is
drawn in the accent and said out loud when it moves.

It is a **coordinate**, never a `juce::ValueTree`. A position survives the edit made under
it, and — the one that would have cost a day — `ProjectEdits::moveClipToTrack` returns a
*new* tree and detaches the one it was given, so a cursor holding a clip would be pointing
at a corpse the moment somebody dragged it to another track. It is also not the selection:
the cursor is where you are, the selection is what you have chosen, and the step grid has
no selection at all and still wants one.

The piano roll's transpose gave up the bare arrows for this and moved to ⌥, which is the
modifier dew already spends on a view's other axis. That is a deliberate change to a
binding somebody may have in their fingers.

**The pointer says what is under it.** `ui/design/Cursors.h` names six cursors for the
gesture rather than for the arrow — `idle`, `clickable`, `value`, `move`, `resizeX`,
`nib` — the way the colours are named for their role, and a source gate refuses a
`juce::MouseCursor::` spelled anywhere else. A clip's or a note's body says it can be
dragged; its right edge says it can be resized; a fader, a knob and a number field say a
vertical drag changes them; a tool outranks all of it, so with paint or slice selected
both canvases show a nib. Clips and notes are not components — each editor paints all of
them into one canvas — so their cursor comes from the same hit test that decides what a
press does, which is what stops it promising something the press will not do.

**A right-click never presses a button.** `juce::Button` completes a click for whichever
mouse button pressed it, so a right-click ran every dew button that had no context menu
of its own. Every button consumes a popup press now, menu or no menu.

**Deleting something that takes others with it asks first** — a pattern and its clips, a
channel and its notes, a playlist track and its clips, a mixer insert and its effects.
Removing an effect does not: it destroys only itself. It is all undoable either way; the
question is about blast radius.

Scrolling and zooming: wheel to scroll, ⌘-wheel or a trackpad pinch to zoom around the
pointer, shift-wheel to scroll in time, ⌘⇧-wheel to zoom the OTHER axis — lane height in
the playlist, pitch-row height in the piano roll. Natural scrolling is honoured, because
the system reports it rather than applying it.

**A notch is a fixed number of pixels, everywhere.** It used to be six *steps*
horizontally, which is 18px zoomed out and 720px zoomed in; one *lane* down the playlist,
which is 34px or 204px; three *rows* down the piano roll, which is 42px; and whatever
JUCE picked in the channel rack and the score tab, which handled the wheel not at all.
Six defensible speeds that disagreed with each other, and about five times slower than
the rest of the machine. Pixels is the only unit all six share, so `wheelPixelsPerNotch`
is the number and each view divides into its own at the point of use.

Zoom is horizontal in every timeline dew has, because time is. **⌥`=`, ⌥`-` and ⌥`0`
size the other axis**: a lane in the playlist, a pitch row in the piano roll, and the
text in the score tab, which has a second size precisely because it has no timeline. One
trio rather than two, on the modifier that leaves a bare `=` free to be an `=` somebody
is typing. Each editor also has the three buttons on its toolbar, and a playlist lane can
be dragged by the bottom edge of its header — one height for every lane, so the edge you
grabbed is only the one the pointer was nearest.

**View → UI Scale** draws the whole interface 100%, 125%, 150% or 175% larger. A
multiplier on the window rather than on the type scale: dew's layout is a ladder of pixel
sizes that a font has to fit inside, so scaling only the text is how a caption ends up
clipped by the box it was measured for.

**Every control says what it is.** The status bar names whatever is under the pointer at
once, and the floating tooltip still arrives after 600ms for anyone who stops — one help
string, two surfaces, both read from the control's own tooltip. A test walks all five
tabs and fails on any control with nothing to say.

Dragging a value: **shift is finer**, on every knob, fader and number field. Shift means
five other things in dew — suspend snap, extend a selection, make a copy unique, transpose
an octave — and every one of them changes a *selection* or a *position*. None changes a
value. That is what keeps the sixth meaning from being one too many. A whole drag is one
undo step.

That sentence was two thirds true for a while, and the exception is worth keeping written
down. The mixer fader is the one value control with no primitive of its own, so it was
also the one nothing told the rule to: a bare `juce::Slider` snaps to the pointer, which
makes a press a *position* rather than a drag, and `setMouseDragSensitivity` — the single
place the shared distance is applied — then does not come into it at all. Both halves are
wired now, and "the fader answers a drag, at the distance every value control uses" in
`tests/MixerUiTests.cpp` is what keeps the sentence honest.

Right-drag erases in the piano roll and the step grid, and means the same thing in both:
one undo step for the sweep, filling the cells between drag samples so a quick flick
leaves no survivors. Alt-drag is the same gesture. A right-press that erased nothing was
never an erase, so in the piano roll it clears the selection instead. In the playlist,
right-click opens a menu instead — a clip is an object with properties and a step is not.

On any ruler: drag to scrub, shift-drag to select a span, ⌘-click to span from the
playhead, shift-click or double-click to drop it. **The span is what plays.**

Right-click a rack row or a track header to rename, add or remove it. **+ Channel** and
**+ Track** sit under the last one, where the next will appear.

In the piano roll: ⌥↑ ⌥↓ transpose a semitone and ⌥⇧↑ ⌥⇧↓ an octave — bare arrows are the cursor; Q quantizes, ⇧R opens
randomize — bare `R` is Record, which has to work from wherever you happen to be looking. Holding shift suspends the snap grid for a drag, which is the only way to
reach an off-grid position without changing the dropdown.

