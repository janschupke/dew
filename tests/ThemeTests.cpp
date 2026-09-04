#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/MainComponent.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Theme.h"
#include "ui/design/Tokens.h"

using namespace dew;
using namespace dew::tokens;

/*  Swapping the palette, and the half of it that does not swap itself.

    Two thirds of the 437 places that read a colour do so inside paint(), and
    those follow a theme with no help. The rest COPIED a colour when they were
    built - a LookAndFeel's ColourIds, a Label's textColourId, a toggle's
    on-colour - and a copy does not follow anything. This is the gate over that
    difference, because "did I find them all" is not a question to answer by
    reading.
*/

namespace
{

/** Every juce ColourId dew ever sets on a component. Probed by hand because
    JUCE offers no way to enumerate what a Component has been given. */
const std::vector<int> probedIds {
    juce::Label::textColourId,
    juce::Label::backgroundColourId,
    juce::TextEditor::backgroundColourId,
    juce::TextEditor::textColourId,
    juce::TextEditor::outlineColourId,
    juce::TextEditor::focusedOutlineColourId,
    juce::TextButton::buttonColourId,
    juce::TextButton::buttonOnColourId,
    juce::TextButton::textColourOffId,
    juce::TextButton::textColourOnId,
    juce::ComboBox::backgroundColourId,
    juce::ComboBox::textColourId,
    juce::ComboBox::outlineColourId,
    juce::ComboBox::arrowColourId,
    juce::Slider::thumbColourId,
    juce::Slider::trackColourId,
    juce::Slider::backgroundColourId,
    juce::ListBox::backgroundColourId,
    juce::CodeEditorComponent::backgroundColourId,
    juce::CodeEditorComponent::lineNumberBackgroundId,
    juce::CodeEditorComponent::lineNumberTextId,
    juce::CodeEditorComponent::defaultTextColourId,
    juce::ToggleButton::textColourId,
    juce::ToggleButton::tickColourId,
    juce::ResizableWindow::backgroundColourId,
    juce::TabbedButtonBar::tabTextColourId,
    juce::TabbedButtonBar::frontTextColourId,
};

/** The colours the dark palette has and high contrast does not, so a value
    found after the swap can only have come from before it. */
std::vector<juce::Colour> darkOnly()
{
    const auto dark = colour::darkPalette();
    const auto high = colour::highContrastPalette();

    std::vector<juce::Colour> only;

    const auto add = [&] (juce::Colour d, juce::Colour h)
    {
        if (d != h)
            only.push_back (d);
    };

    add (dark.wellDeep, high.wellDeep);
    add (dark.well, high.well);
    add (dark.background, high.background);
    add (dark.surface, high.surface);
    add (dark.surfaceRaised, high.surfaceRaised);
    add (dark.surfaceHover, high.surfaceHover);
    add (dark.divider, high.divider);
    add (dark.dividerStrong, high.dividerStrong);
    add (dark.outline, high.outline);
    add (dark.textPrimary, high.textPrimary);
    add (dark.textSecondary, high.textSecondary);
    add (dark.textDisabled, high.textDisabled);
    add (dark.textOnAccent, high.textOnAccent);
    add (dark.accent, high.accent);
    add (dark.accentMuted, high.accentMuted);
    add (dark.recording, high.recording);
    add (dark.success, high.success);
    add (dark.danger, high.danger);
    add (dark.keyBlack, high.keyBlack);
    add (dark.keyWhite, high.keyWhite);
    add (dark.beatShade, high.beatShade);
    add (dark.barShade, high.barShade);

    return only;
}

juce::String describe (juce::Component& c)
{
    juce::StringArray path;

    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (p->getComponentID().isNotEmpty())
            path.insert (0, p->getComponentID());

    if (path.isEmpty())
        path.add ("(no id)");

    return path.joinIntoString (" > ");
}

void walk (juce::Component& root, const std::function<void (juce::Component&)>& visit)
{
    for (auto* child : root.getChildren())
    {
        visit (*child);
        walk (*child, visit);
    }
}

} // namespace

TEST_CASE ("a palette is one assignment", "[design][theme]")
{
    // The mechanism, on its own: the names are references into the palette in
    // force, so assigning the palette changes what every one of them reads.
    const auto dark = colour::darkPalette();
    REQUIRE (colour::accent == dark.accent);

    theme::applyPalette (theme::Kind::highContrast);
    CHECK (colour::accent == colour::highContrastPalette().accent);
    CHECK (colour::accent != dark.accent);

    theme::applyPalette (theme::Kind::dark);
    CHECK (colour::accent == dark.accent);
    CHECK (theme::current() == theme::Kind::dark);
}

TEST_CASE ("high contrast is the same design further apart", "[design][theme]")
{
    // Every role survives the swap. A theme that dropped one - left a colour
    // default-constructed, say - would be transparent black, and the pixel
    // tests elsewhere would fail somewhere far from the cause.
    const auto high = colour::highContrastPalette();

    CHECK (high.textPrimary.isOpaque());
    CHECK (high.accent.isOpaque());
    CHECK (high.funcPitch.isOpaque());

    // Darker grounds and lighter text than the default palette: that IS the
    // theme, and stating it here is what stops a later edit quietly narrowing
    // the distance the ContrastTests thresholds are supposed to guarantee.
    const auto dark = colour::darkPalette();

    CHECK (high.wellDeep.getBrightness() <= dark.wellDeep.getBrightness());
    CHECK (high.textPrimary.getBrightness() >= dark.textPrimary.getBrightness());
    CHECK (high.outline.getBrightness() > dark.outline.getBrightness());
}

TEST_CASE ("nothing keeps a colour from the palette it was built under", "[design][theme]")
{
    // The gate the other two exist to support. A component that copied a colour
    // in its constructor is holding the palette that was in force then, and
    // will go on holding it - so the window would come back half themed, in
    // whichever places nobody thought of.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    theme::applyPalette (theme::Kind::dark);

    MainComponent component (false);
    component.setSize (1400, 900);

    theme::apply (theme::Kind::highContrast, component);

    const auto stale = darkOnly();
    juce::StringArray holdouts;
    auto probed = 0;

    walk (component,
          [&] (juce::Component& c)
          {
              for (const auto id : probedIds)
              {
                  if (! c.isColourSpecified (id))
                      continue;

                  ++probed;
                  const auto held = c.findColour (id, false);

                  for (const auto& old : stale)
                      if (held == old)
                          holdouts.addIfNotAlreadyThere (describe (c) + "  colourId "
                                                         + juce::String (id) + " still "
                                                         + held.toDisplayString (false));
              }
          });

    theme::apply (theme::Kind::dark, component);

    // A control case: a walk that probed nothing would pass for the wrong
    // reason, and every one of these is a colour somebody set by hand.
    INFO (probed << " specified colours probed");
    REQUIRE (probed > 5);

    INFO ("components still painted from the old palette:\n" << holdouts.joinIntoString ("\n"));
    CHECK (holdouts.isEmpty());
}
