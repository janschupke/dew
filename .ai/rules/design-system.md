# Design system

**Read `tests/SourceGateTests.cpp` before writing any new UI file.** Most of the gates are
tagged `[design]`, and each one exists because a second vocabulary had grown beside the
first. A new component must take every dimension, colour, duration and gesture from
`src/ui/design/Tokens.h` and `src/ui/design/Gestures.h`.

The vocabulary is `dew::tokens`, split into `colour`, `emphasis`, `space`, `radius`,
`stroke`, `icon`, `type`, `size` and `motion`. `src/ui/primitives/` holds the controls
built on it, in four headers rather than one grab-bag: `ButtonBehaviour.h` is how a
control BEHAVES (the hover easing, the right-button refusal, the focus note),
`DewButtons.h` is everything a person clicks that is not a knob, `DewKnob.h` is the rotary
and the slider under it, and `DewPaint.h` is what the system draws. Include the one you
use. [Why it is this way](#why-it-is-this-way), below, argues why.

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

## Rows of knobs

**Knobs are laid out by `src/ui/KnobGrid.h`, and by nothing else.** Five panels each had
a lambda that divided a rectangle by a compile-time count, which is how the instrument
panel came to draw four envelope knobs at a quarter of its width and the two level knobs
directly below them at a half. Two rules come out of it:

- **One cell width across a whole grid**, capped at `size::knobColumn` and reflowed onto
  another row below `size::knobColumnMin`. Spare width goes into the gaps *around* the
  groups, never into the cells: six knobs in a wide panel are six knobs and some air.
- **A group is never split while it fits a row.** Two groups sharing a row are separated
  by a rule, drawn by the caller at the rectangles `place()` returns — the split
  `StripLayout::divider` already keeps. Two groups on different rows are separated by the
  **row break** and get no rule, because a rule as well would say it twice.

The argument for each is under `KnobGrid.h`'s own comment. What a cell *holds* stays the
caller's: a knob fills it, a number field is a fixed-height control centred in it.

The mixer's effect band is a whole number of knob rows (`size::effectBandRows{Min,Default,
Max}`), draggable by the rule along its top. Its height lives on `MixerComponent` and is
stored raw in `Settings` — **view geometry lives on the view**, and `dew_app` cannot see
the ladder to clamp against. See [gestures-and-hotkeys.md](gestures-and-hotkeys.md).

## Icons and the gallery

Icons are `juce::Path` in `src/ui/design/icons/*.cpp`, never shipped assets. Adding one
is a function, a declaration in `Icons.h` and a row in `icons::all()` — an icon left out
of that list is drawn by nothing and checked by nothing. Any grid dimension must derive
from `icons::all().size()` — the gallery's icon grid was once a hard-coded two rows, and
three new icons drew straight over the section below, on the one page whose job is to
show what the design system looks like.

**Which glyph a CONCEPT wears is `src/ui/design/Glyphs.h`, and only there.**
`glyph::forEffect`, `forInstrument`, `forWaveform` and `forAction` are switches with no
`default`, so a new kind of thing is a compile error rather than one that silently wears
the first shape in the list. This is `ParamPalette.h` for pictures, and the same rule
applies: `Icons.h` is the vocabulary and knows nothing but JUCE, so the join between it
and the document model lives in its own header.

**A menu row's glyph travels in `PopupMenu::Item::image`, as a `MenuGlyph`** — see
`src/ui/design/MenuGlyph.h`. Add rows with `addGlyphItem` / `addGlyphSubMenu`, which is
what makes a row with no picture visible as a plain `addItem` at the call site. Two rules
hold:

- **A menu is fully glyphed or not glyphed at all**, the same rule the tick gutter
  follows: a menu where some rows are indented and others are not reads as misaligned.
- **The glyph is painted in the row's own text colour**, never its own. That is what
  makes it legible on the accent fill *and* what keeps it inside the contrast
  `ContrastTests` has already proven for menu text — a glyph with a colour of its own
  would be a new pair to answer for in both palettes.

Do NOT smuggle a glyph's name into the item text the way `menuRow` smuggles a second
line. A sentinel there leaks out through `ComboBox::getText`, through the accessible
name, and through `dew::menuItems` — the seam every menu test in the repository reads.

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

**A knob can be typed into.** Double-click its readout and a box opens over it, seeded
with the plain number the readout shows and no unit — a box holding `0.140 s` is a box
whose contents do not parse. It is `src/ui/primitives/TypedEdit.h`, shared with
`DewNumberField`, because the parts that go wrong when this is written twice are the
three ways out of it: return keeps, escape does not, losing focus keeps. A compact knob
draws no readout and gets none of it.

`TypedEdit` destroys its editor from inside that editor's own callback, so its commit
function is a MEMBER rather than a lambda capture — a captured one is destroyed with the
`std::function` it is running inside, before it can be called.

**A spec-built knob takes its tooltip from `ParamSpec::displayName`** — `DewKnob (const
ParamSpec&)` applies it, along with the range, the interval, the decimals and the
bipolarity. Do not hand-write any of those for a knob built from the catalog; a gate
refuses `…Knob.setBipolar` and `…Knob.setNumDecimalPlaces`. Twenty-seven calls were
restating a spec they were built from, and the reason to delete them is not that they had
drifted — they had not — but that a restated number is only correct until it is not. And a
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
- **A component a person would name as one thing is a group**: it declares
  `setFocusContainerType (FocusContainerType::focusContainer)` and it calls `setTitle`. An
  effect card, a mixer strip, a rack row, a track header, each panel. Two gates hold it —
  "every control in the window belongs to a group" and "every group has a name a screen
  reader can read" — and ⌃⇥ steps over the same list, so the keyboard and the screen
  reader read one structure rather than two. Never the *keyboard* focus container: it
  confines ⇥ with no key to leave, which is a trap, and dew reads that distinction to tell
  its own groups from the ones `juce::Label` and `juce::ScrollBar` declare for themselves.
- **A value control answers arrows itself.** `juce::Slider::keyPressed` steps by
  `getInterval()` and refuses every modifier, so on a `0.001` interval it was a thousand
  presses end to end and shift blocked the edit rather than refining it. `DewSlider` and
  `DewNumberField` override it — see
  [gestures-and-hotkeys](gestures-and-hotkeys.md#changing-a-value-from-the-keyboard).
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

## Why it is this way

`src/ui/design/` holds the vocabulary — colour roles, spacing, type, a size ladder, an
*emphasis* scale, motion durations, and forty-odd icons drawn as `juce::Path` rather
than shipped as assets. `src/ui/primitives/` holds the controls built on it.

`dew_shot gallery out.png` renders every token, icon and primitive in every state onto one
page, which is both how the design system is reviewed and how it is tested.

Eight source-scanning tests keep the vocabulary whole, and each one exists because a
second vocabulary had grown beside the first: no colour written as hex, no emphasis as a
bare number, no radius or stroke as a bare number, no gap or inset off the spacing scale,
no component redeclaring a dimension the ladder already names, no timer picking its own
refresh rate, no font built outside `Tokens.cpp` — and one that refuses the opposite
mistake, a token nothing refers to.

Four more hold coverage rather than vocabulary, and each exists because a control added to
a panel without them is exactly the omission nobody notices: every spec-built knob in the
window has a right-click menu, and every control in all five tabs has help text, an
accessible name, and a way for the keyboard to reach it. The second found thirty of
sixty-three silent when it was written; the third and fourth are below.

A fifth holds the palette to a number rather than to a vocabulary. Colours are named for
their ROLE, which is what makes a theme one assignment — but a role says nothing about
whether the pair is legible, and five of them were not. `ContrastTests` states the pairs
that are actually painted and the ratio each needs, **for every palette at that palette's
own thresholds**, so a token cannot be darkened back without an argument and a new theme
cannot be added without clearing the same bar. The worst of the original five was the
hover-help line itself: the app's only always-on explanation of the control under the
pointer, drawn in the palette's least readable colour at 2.6:1.

### Two palettes

**View → Theme**, and **Preferences → Appearance**, which invokes the same command rather
than applying a palette of its own. The default is dew as it has always looked, held to
WCAG AA — 4.5:1 for anything read, 3:1 for an edge you have to find. **High contrast** is the same design with
the distances opened up, held to AAA: 7:1 and 4.5:1.

Every value in it is derived rather than chosen by eye. The surfaces were pushed down and
apart first; then each meaning and function colour kept its hue and saturation and had
only its lightness raised, by bisection, until it cleared its target against `surfaceHover`
— the lightest ground anything is drawn on, so clearing it clears the other five. Keeping
hue and saturation is the point: a high-contrast theme that also re-hued everything would
be a second design to maintain, and this one is the same design further apart.

It stays **dark**, and that is what makes it small. A light theme is a different job:
`emphasis::silenced` and `emphasis::disabled` both multiply brightness downward, the four
lift rungs mean "how much brighter", and `wellDeep` is used as a scrim at four sites. All
of that is correct on a dark ground and inverts on a light one.

`channelRamp` is **not** themed. Those eight colours are document data — `entityColour`
writes them into every `.dew` file, `dew_model` restates them as strings, and the colour
picker offers them — so repainting them would make every saved project disagree with the
swatch it was chosen from. What varies is `textOnAccent`, drawn on top, and that is why
the clip-label pair is the one thing held to AA in both themes.

The mechanism is worth knowing before adding a colour. The names in `tokens::colour` are
**references** into the palette in force, so the 437 places that read one need no edit and
a theme is a single assignment. Two thirds of those reads happen inside `paint()` and
follow it for free; the rest COPIED a colour when they were built — a LookAndFeel's
ColourIds, a Label's `textColourId`, a toggle's on-colour — and a copy follows nothing.
`theme::apply` re-seeds the look and feel and then calls `sendLookAndFeelChange`, which is
JUCE's own hook for exactly this, and a gate walks the window after a switch and fails on
anything still holding a colour from the palette it was built under. That gate found ten
sites the first time it ran.

### Reaching it without a mouse

`juce::Slider`'s constructor turns keyboard focus off, so every knob in dew was
unreachable by tab and `Slider::keyPressed` — the arrows, page up and down — was dead code
in all of them. The editor toolbars then refused focus outright, to stop a *click* moving
focus off the roll and killing the shortcuts it owns; that is what
`setMouseClickGrabsKeyboardFocus` is for, and the two things were being spelled with one
call. Both are gates now: every control in all five tabs wants keyboard focus, unless it
is disabled.

A focused control draws an accent ring. It is painted by the primitive rather than through
`LookAndFeel::createFocusOutlineForComponent`, which puts the ring in its own overlay
window and so needs a `ComponentPeer` — the same reason the animator is a `Timer`. JUCE's
version would be invisible to the suite and to `dew_shot`, which is to say untestable in
the two places this codebase looks at its own pixels. `paint::focusRing` takes the focus
flag as an argument for the same reason: `grabKeyboardFocus` does nothing without a peer,
so a helper that asked for itself could never be shown to draw.

The tooltip is the accessible name. dew already had one curated sentence per control and a
gate refusing a control without one, so a screen reader reads that sentence rather than a
second vocabulary nobody keeps in step — `setTooltip` sets both on every primitive. It is
an override rather than a convention because the convention had already failed: three zoom
buttons were constructed with an empty label and given their tooltip a line later, so the
status bar explained them and a screen reader found nothing. A knob is the awkward case —
it is a `juce::Component` wrapping the `juce::Slider` that carries the role, the range and
the value — so the wrapper returns an *ignored* handler. Not `setAccessible (false)`:
`Component::isAccessible` walks up to its parent, so switching the wrapper off would take
the slider inside it off too.

Everything above is plain portable code. JUCE implements accessibility natively on macOS
and Windows and compiles the same calls to nothing where there is no backend, so none of
it is behind an `#ifdef`.

`dew_shot gallery` earns its place the same way. The icon grid's height was a hard-coded
two rows, so three new icons drew straight over the section below — on the one page whose
whole job is to show what the design system looks like.

### Motion

Every eased value is stepped from one clock. Two decisions shape it.

It is a `juce::Timer` rather than a `VBlankAttachment`, because a vblank needs a
`ComponentPeer` and every UI test here paints into an `Image` with no peer and no message
loop: a design that cannot run where the suite runs is one the suite cannot check.

And **animation is off unless the application turns it on** — deliberately the wrong way
round from how it looks, so every headless test and every `dew_shot` render behaves
exactly as it did before the animator existed. Motion is a property of a running
application, not of a widget. `Animator::advance (deltaMs)` steps every client by a
chosen number of milliseconds with no wall clock, so a test walks a whole interaction
frame by frame rather than sampling it at the ends. Reduce motion sets every duration to
zero, which makes `animateTo` identical to `snapTo` — no call site needs a branch.

Reduce motion is **View → Motion**, and **Preferences → Appearance**, and it is three
states rather than two: follow the system, full motion, reduce motion. A stored boolean cannot say "follow the OS", so
reading the preference into one at startup would silently overwrite a choice made in dew,
and reading it only when the file had no value would mean a preference turned on later
never arrived. `system` is the default.

Asking the OS is dew's one piece of per-OS code — `systemPrefersReducedMotion`, in
`ui/design/`. JUCE wraps dark mode portably and stops there, so this is a preference read
on macOS and on Windows and `false` where there is nothing to ask. It is a `.cpp` reading
CFPreferences rather than a `.mm` reading `NSWorkspace`, because the Objective-C version
would put `OBJCXX` in the project's languages for one boolean.

A knob has three rules, in priority order: animation is off unless turned on; a **drag is
never eased**, because a needle trailing the pointer moving it feels broken; and the
**first** value a knob is given snaps, or a panel built from a document sweeps every knob
up from zero. The wheel is never eased at all — adding lag to the one gesture that must
feel direct is a regression, not a polish.

