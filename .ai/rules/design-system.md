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

Two more tests exist because a control added to a panel without them is the omission
nobody notices: every spec-built knob has a right-click menu, and every control in every
tab has help text.

**A spec-built knob takes its tooltip from `ParamSpec::displayName`** — `DewKnob (const
ParamSpec&)` applies it. Do not hand-write help for a knob built from the catalog. And a
knob is a `juce::SettableTooltipClient` itself: `setTooltip` must set both it and the
slider inside it, because `juce::TooltipWindow` hit-tests the deepest component under the
pointer.
