#include "ui/design/Theme.h"

#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Tokens.h"

namespace dew::theme
{

namespace
{
Kind inForce = Kind::dark;
}

Kind current() noexcept
{
    return inForce;
}

juce::String name (Kind kind)
{
    return kind == Kind::highContrast ? "highContrast" : "dark";
}

Kind kindFor (const juce::String& stored)
{
    return stored == "highContrast" ? Kind::highContrast : Kind::dark;
}

void applyPalette (Kind kind)
{
    inForce = kind;

    tokens::colour::active = kind == Kind::highContrast ? tokens::colour::highContrastPalette()
                                                        : tokens::colour::darkPalette();
}

void apply (Kind kind, juce::Component& root)
{
    applyPalette (kind);

    // The LookAndFeel first: sendLookAndFeelChange makes components ask it for
    // colours again, so it has to be holding the new ones before they do.
    if (auto* dewLookAndFeel = dynamic_cast<DewLookAndFeel*> (
            &juce::Desktop::getInstance().getDefaultLookAndFeel()))
        dewLookAndFeel->applyPalette();

    root.sendLookAndFeelChange();

    // And laid out again, the way applyUiScale already does. A ComboBox's text
    // colour is put on its internal label by positionComboBoxText, which runs
    // on layout rather than on paint, so a box left alone keeps the old
    // palette's text until something else happens to move it.
    root.resized();
    root.repaint();
}

} // namespace dew::theme
