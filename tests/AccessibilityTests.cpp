#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ModuleCatalog.h"
#include "ui/MainComponent.h"
#include "ui/design/Focus.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"
#include "ControlWalkHarness.h"
#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;

/*  What a screen reader is told about dew.

    JUCE implements accessibility natively on macOS and Windows and compiles the
    same calls to nothing on Linux, so everything asserted here is plain
    portable code - these gates hold the CONTENT of the tree, which is the half
    JUCE cannot supply and the half that rots.
*/

TEST_CASE ("every control in the window has a name a screen reader can read", "[ui][a11y]")
{
    // The sibling of "every control says what it is", and deliberately a second
    // gate rather than a second assertion inside that one: a control can have a
    // tooltip and no accessible name, and for six of them it did. setTooltip
    // sets the TooltipClient string; juce::Button seeds its name from the
    // constructor argument, once, and three buttons were constructed with an
    // empty one and given their tooltip afterwards.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1400, 900);

    juce::StringArray unnamed;

    const auto controls = forEachControl (component,
                                          [&] (juce::Component& c)
                                          {
                                              // The knob is the case the walk has to be careful
                                              // with: it is a Component wrapping the juce::Slider
                                              // that IS the control, and the name lives on the
                                              // slider, where the role and the value are.
                                              const auto* named = [&]() -> const juce::Component*
                                              {
                                                  if (auto* knob = dynamic_cast<DewKnob*> (&c))
                                                      return &knob->getSlider();

                                                  return &c;
                                              }();

                                              const auto name = named->getTitle().isNotEmpty()
                                                                    ? named->getTitle()
                                                                    : named->getName();

                                              if (name.isEmpty())
                                                  unnamed.addIfNotAlreadyThere (describe (c));
                                          });

    // A control case: a walk that found nothing would pass for the wrong reason,
    // and so would one that only ever saw a single tab.
    INFO (controls << " controls, " << (controls - unnamed.size()) << " named");
    REQUIRE (controls > 100);

    INFO ("controls a screen reader would meet unnamed:\n" << unnamed.joinIntoString ("\n"));
    CHECK (unnamed.isEmpty());
}

TEST_CASE ("every control in the window can be reached by the keyboard", "[ui][a11y]")
{
    // juce::Slider's constructor turns keyboard focus OFF, so every knob in the
    // application was unreachable by tab and Slider::keyPressed was dead code in
    // all of them. The toolbars then refused focus outright to stop a CLICK
    // moving it off the editor, which is what setMouseClickGrabsKeyboardFocus
    // is for - the two were being spelled with one call.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1400, 900);

    juce::StringArray unreachable;

    const auto controls = forEachControl (component,
                                          [&] (juce::Component& c)
                                          {
                                              const auto* focusable =
                                                  [&]() -> const juce::Component*
                                              {
                                                  if (auto* knob = dynamic_cast<DewKnob*> (&c))
                                                      return &knob->getSlider();

                                                  return &c;
                                              }();

                                              // A DISABLED control is not operable, so nothing
                                              // requires a tab key to reach it - and JUCE's
                                              // traverser skips it anyway. The default project has
                                              // one pattern and no soundfont, so "delete this
                                              // pattern" and the preset list are both legitimately
                                              // off.
                                              if (! c.isEnabled())
                                                  return;

                                              if (! focusable->getWantsKeyboardFocus())
                                                  unreachable.addIfNotAlreadyThere (describe (c));
                                          });

    INFO (controls << " controls, " << (controls - unreachable.size()) << " reachable");
    REQUIRE (controls > 100);

    INFO ("controls no tab key can reach:\n" << unreachable.joinIntoString ("\n"));
    CHECK (unreachable.isEmpty());
}

TEST_CASE ("a knob presents itself as one slider, not a group around one", "[ui][a11y]")
{
    // setAccessible (false) would be the wrong tool and this is the test that
    // says so: Component::isAccessible walks UP to its parent, so switching the
    // wrapper off takes the slider inside it off with it. `ignored` is the flag
    // that means "skip me, keep my children".
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewKnob knob { "Cutoff", 0.0, 1.0, 0.01 };
    knob.setSize (tokens::size::knob, tokens::size::knobRow);
    knob.setTooltip ("How bright the filter lets the sound be");

    // createAccessibilityHandler rather than getAccessibilityHandler: the latter
    // returns null without a ComponentPeer, and this harness has none - the
    // same reason the focus ring is painted by the primitive rather than by
    // JUCE's overlay window.
    const auto wrapper = knob.createAccessibilityHandler();
    REQUIRE (wrapper != nullptr);
    CHECK (wrapper->getRole() == juce::AccessibilityRole::ignored);

    // The slider inside is still there, still a slider, and named.
    const auto inner = knob.getSlider().createAccessibilityHandler();
    REQUIRE (inner != nullptr);
    CHECK (inner->getRole() == juce::AccessibilityRole::slider);
    CHECK (inner->getTitle() == "How bright the filter lets the sound be");

    // And it carries the value, which is the whole reason the slider rather
    // than the wrapper is what a screen reader should be meeting.
    auto* value = inner->getValueInterface();
    REQUIRE (value != nullptr);
    CHECK (value->getRange().isValid());
}

TEST_CASE ("a control's tooltip and its accessible name are the same sentence", "[ui][a11y]")
{
    // Set AFTER construction, which is the case that was silently broken: three
    // zoom buttons were built with an empty label and told their tooltip a line
    // later, so the status bar explained them and a screen reader did not.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewIconButton icon { juce::Path(), {} };
    CHECK (icon.getTitle().isEmpty());

    icon.setTooltip ("Fit the song to the window");
    CHECK (icon.getTitle() == "Fit the song to the window");
    CHECK (icon.getTooltip() == "Fit the song to the window");

    // A letter toggle needs it most: its button text is "M".
    DewLetterToggle letter { "M", tokens::colour::warning, {} };
    letter.setTooltip ("Mute this channel");
    CHECK (letter.getTitle() == "Mute this channel");
}

namespace
{

/** A surface that runs one paint:: helper, so the ring can be measured without
    a control and its document behind it.
*/
class RingProbe : public juce::Component
{
public:
    explicit RingProbe (bool f)
        : focused (f)
    {
        setSize (120, tokens::size::controlHeight);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (tokens::colour::surfaceRaised);

        // The predicate a primitive actually passes, not the raw flag: whether
        // a control HAS the keyboard and whether it should say so are two
        // questions, and this probe is the only place both are measurable.
        paint::focusRing (g, *this, focus::ringVisibleFor (focused));
    }

private:
    const bool focused;
};

} // namespace

TEST_CASE ("a focused control draws a ring, and an unfocused one draws none", "[ui][a11y][focus]")
{
    // WCAG 2.4.7. Before this, one control in the whole application showed
    // focus - the combo box - so a keyboard user could tab through it and never
    // learn where they were.
    //
    // Drawn by the primitive rather than through JUCE's
    // createFocusOutlineForComponent, which puts the ring in its own overlay
    // window and so needs a peer: this harness has none, and neither does
    // dew_shot. That is why the ring is measurable here at all.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RingProbe unfocused { false };
    RingProbe focused { true };

    focus::noteFocusChange (juce::Component::focusChangedByTabKey);

    const auto without = testing::coverageOf (testing::render (unfocused), tokens::colour::accent);
    const auto with = testing::coverageOf (testing::render (focused), tokens::colour::accent);

    // exactlyEqual, because the ci preset builds with -Wfloat-equal as an error.
    CHECK (juce::exactlyEqual (without, 0.0f));
    CHECK (with > 0.0f);
}

TEST_CASE ("a ring is drawn for the keyboard and not for the mouse", "[ui][a11y][focus]")
{
    // A ring answers "where will the next keystroke go", which is a question
    // you only have while your hands are on the keyboard - the pointer already
    // says where the next click goes by being where it is. Every primitive
    // ended its paint with hasKeyboardFocus alone, so clicking a knob ringed it
    // in the same accent the app uses for selection and left it there.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RingProbe focused { true };

    focus::noteFocusChange (juce::Component::focusChangedByMouseClick);
    const auto afterClick = testing::coverageOf (testing::render (focused), tokens::colour::accent);

    focus::noteFocusChange (juce::Component::focusChangedByTabKey);
    const auto afterTab = testing::coverageOf (testing::render (focused), tokens::colour::accent);

    CHECK (juce::exactlyEqual (afterClick, 0.0f));
    CHECK (afterTab > 0.0f);

    // focusChangedDirectly is a deliberate move that was not a click - a view
    // handing focus on, a dialog opening - and reads as the keyboard. The three
    // canvases reach it from their own mouseDown and paint no ring either way:
    // they draw paint::cursorOutline, which CanvasCursor::isPlaced refuses
    // until the keyboard has placed it.
    focus::noteFocusChange (juce::Component::focusChangedDirectly);
    CHECK (testing::coverageOf (testing::render (focused), tokens::colour::accent) > 0.0f);
}

TEST_CASE ("a primitive tells the design system what moved the keyboard onto it",
           "[ui][a11y][focus]")
{
    // The half the probe above cannot cover: that the controls actually REPORT
    // it. grabKeyboardFocus does nothing without a ComponentPeer and this
    // harness has none, so the hook is driven directly - which is the same
    // reason paint::focusRing takes its flag as an argument.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewButton button { "Play" };
    DewIconButton icon { icons::zoomIn(), {} };
    DewLetterToggle letter { "M", tokens::colour::warning, "Mute" };
    DewDropdown dropdown;
    DewNumberField field;

    struct Named
    {
        const char* name;
        juce::Component* control;
    };

    const Named controls[] {
        { "DewButton", &button },     { "DewIconButton", &icon },   { "DewLetterToggle", &letter },
        { "DewDropdown", &dropdown }, { "DewNumberField", &field },
    };

    for (const auto& c : controls)
    {
        INFO (c.name);

        focus::noteFocusChange (juce::Component::focusChangedByTabKey);
        c.control->focusGained (juce::Component::focusChangedByMouseClick);
        CHECK_FALSE (focus::ringVisible());

        focus::noteFocusChange (juce::Component::focusChangedByMouseClick);
        c.control->focusGained (juce::Component::focusChangedByTabKey);
        CHECK (focus::ringVisible());
    }

    // A knob is the awkward one: the wrapper paints the ring and the slider
    // inside it carries the keyboard, so focusGained never fires on the thing
    // that draws. focusOfChildComponentChanged is the only hook there is.
    const auto& spec = requireInstrumentParamSpec (ids::volume);
    DewKnob knob { spec };

    focus::noteFocusChange (juce::Component::focusChangedByTabKey);
    knob.focusOfChildComponentChanged (juce::Component::focusChangedByMouseClick);
    CHECK_FALSE (focus::ringVisible());

    focus::noteFocusChange (juce::Component::focusChangedByMouseClick);
    knob.focusOfChildComponentChanged (juce::Component::focusChangedByTabKey);
    CHECK (focus::ringVisible());
}
