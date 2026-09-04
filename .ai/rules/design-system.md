# Design system

**Read `tests/SourceGateTests.cpp` before writing any new UI file.** Most of the gates are
tagged `[design]`, and each one exists because a second vocabulary had grown beside the
first. A new component must take every dimension, colour, duration and gesture from
`src/ui/design/Tokens.h` and `src/ui/design/Gestures.h`.

The vocabulary is `dew::tokens`, split into `colour`, `emphasis`, `space`, `radius`,
`stroke`, `icon`, `type`, `size` and `motion`. `src/ui/primitives/` holds the controls
built on it. [README.md](../../README.md#design-system) argues why.

## What a gate will refuse

- **A colour written as hex.** Take a role from `tokens::colour`.
- **An emphasis as a bare number**, a **radius or stroke as a bare number**, a **gap or
  inset off the spacing scale** (`removeFromTop (6)`, `reduced (10)`).
- **A dimension the size ladder already names.** A `constexpr int ...Height` / `...Width` /
  `...Thickness` / `...Depth` / `...Gutter` whose value equals a rung is a second copy of
  that rung, wherever it is written.
- **A timer picking its own refresh rate.** One clock.
- **A bare `juce::ComboBox` or `juce::ToggleButton`.** Use `src/ui/primitives/`.
- **A mouse cursor outside the vocabulary** in `src/ui/design/Cursors.h`.
- **A token nothing refers to.** The opposite mistake: adding a token on speculation fails
  too, and the "awaiting" allowlist inside that gate may only ever get shorter.
- **A colour pair below its contrast ratio.** `tests/ContrastTests.cpp` states the pairs
  that are actually painted and what each needs, **for every palette at its own
  thresholds** — 4.5:1 / 3:1 for the default, 7:1 / 4.5:1 for high contrast. A role name
  says nothing about whether the pair is legible; five of them were not. Changing a
  `colour::` value, or adding a theme, means answering that table.

## Icons and the gallery

Icons are `juce::Path` in `src/ui/design/Icons.cpp`, never shipped assets. Any grid
dimension must derive from `icons::all().size()` — the gallery's icon grid was once a
hard-coded two rows, and three new icons drew straight over the section below, on the one
page whose job is to show what the design system looks like.

**Render `dew_shot gallery out.png` after touching a token, an icon or a primitive**, and
look at it. A layout defect is obvious in a picture and nearly invisible in code, and
screen-recording permission is not always available. See [testing.md](testing.md).

## Motion

One `Animator` clock (`src/ui/design/Animator.*`), a `juce::Timer` rather than a
`VBlankAttachment` because a vblank needs a `ComponentPeer` and every UI test here paints
into a peer-less `Image`.

- **Animation is off unless the application turns it on** — deliberately the wrong way
  round from how it looks, so every headless test and every `dew_shot` render behaves as
  it did before the animator existed.
- `Animator::advance (deltaMs)` steps every client by a chosen number of milliseconds with
  **no wall clock**, so a test walks a whole interaction frame by frame rather than
  sampling it at the ends. Write motion tests that way.
- Reduce motion sets every duration to zero, which makes `animateTo` identical to
  `snapTo`. No call site branches on it.
- A knob, in priority order: animation is off unless turned on; **a drag is never eased**;
  the **first** value a knob is given snaps. **The wheel is never eased at all** — see
  [gestures-and-hotkeys.md](gestures-and-hotkeys.md).

## Coverage, not vocabulary

Four more tests exist because a control added to a panel without them is the omission
nobody notices: every spec-built knob has a right-click menu, and every control in every
tab has help text, an accessible name, and a way for the keyboard to reach it. All four
walk the window through `tests/ControlWalkHarness.h` — see [testing.md](testing.md).

**A spec-built knob takes its tooltip from `ParamSpec::displayName`** — `DewKnob (const
ParamSpec&)` applies it. Do not hand-write help for a knob built from the catalog. And a
knob is a `juce::SettableTooltipClient` itself: `setTooltip` must set both it and the
slider inside it, because `juce::TooltipWindow` hit-tests the deepest component under the
pointer.

## Reaching a control without a mouse

- **`juce::Slider`'s constructor turns keyboard focus OFF.** A control built on one is
  unreachable by tab, and `Slider::keyPressed` is dead code in it, until you say
  `setWantsKeyboardFocus (true)`.
- **To stop a click moving focus, say `setMouseClickGrabsKeyboardFocus (false)`**, not
  `setWantsKeyboardFocus (false)`. The toolbars used the second to get the first and took
  themselves out of the tab order to do it.
- **A focused control ends its paint with `paint::focusRing (g, *this,
  hasKeyboardFocus (true))`.** It takes the flag rather than reading it, because
  `grabKeyboardFocus` does nothing without a `ComponentPeer` and a helper that asked for
  itself could never be shown to draw. JUCE's own
  `LookAndFeel::createFocusOutlineForComponent` is an overlay window, so it is invisible
  to the suite and to `dew_shot` for the same reason.
- **`setTooltip` sets the accessible name too**, on every primitive, so a screen reader
  reads the one sentence the codebase already curates. It is an override on each primitive
  rather than a convention: three buttons were built with an empty label and given their
  tooltip a line later. On a `DewDropdown` call `juce::ComboBox::setTooltip`, not
  `SettableTooltipClient::setTooltip` — a ComboBox keeps its tooltip on the label inside
  it and reads `getTooltip` back from there.
- **A wrapper around the real control returns an IGNORED handler**, as `DewKnob` does.
  Never `setAccessible (false)`: `Component::isAccessible` walks up to its parent, so
  switching a wrapper off takes its children off with it.
- Everything here is portable. JUCE implements accessibility natively on macOS and Windows
  and compiles the same calls to nothing elsewhere, so none of it needs an `#ifdef`. The
  one exception in the tree is `systemPrefersReducedMotion`, because JUCE has no API for
  that preference on any platform.

## Adding or changing a colour

- **The names in `tokens::colour` are REFERENCES into the palette in force**, defined in
  `Tokens.h`; the values live in `Tokens.cpp` as two `Palette` functions written with
  designated initialisers. Both files are exempt from the hex gate and nothing else is.
  Adding a colour means adding a member, a value in **both** palettes, and a reference.
- **Anything that COPIES a colour must take it again on `lookAndFeelChanged()`.** Reading
  one inside `paint()` is free and follows a theme by itself; a `setColour` on a child, a
  member initialised from a token, a LookAndFeel ColourId — all of those are the palette
  that was in force when they ran. `DewLabel` exists for the label case: it holds the
  token's address, which is stable, and takes its value again on demand.
  `theme::apply` walks the window and a gate fails on anything left behind.
- **Call the base class in a `lookAndFeelChanged()` override.**
  `juce::ComboBox::lookAndFeelChanged` replaces its label outright, so an override that
  skipped it would leave the old one, with the old palette on it, in place.
- **`channelRamp` is not themeable.** It is document data, mirrored in `dew_model` and
  written into every project file. A cross-layer test holds the two copies equal.
- **A theme is dark.** `emphasis::silenced`, `emphasis::disabled` and the four lift rungs
  all encode a direction — less is darker, hovered is lighter — which is true on a dark
  ground and inverts on a light one. A light theme is a change to the emphasis vocabulary,
  not to the palette.
- **`dew_shot --theme highContrast`** renders any tab or the gallery under a theme. Look at
  it after touching a colour.
