// =============================================================================
// TransportBar - the two switches: the click, and the letter keys.
//
// The same class, a third translation unit beside TransportBarMeter.cpp, and
// split out for the same reason it was: TransportBar.cpp is at the four hundred
// code lines the tree allows a file.
//
// It is also the seam that was already there. Both of these are MODES rather
// than actions - nothing in the bar is lit while it is not pressed except these
// two - and both are polled rather than written by their own click, because the
// menu bar, a hotkey and an MCP client can all move them from somewhere else.
// The count-in hangs off the metronome because that is where a person looks for
// it, and it is the only thing on this bar with a right-click of its own.
// =============================================================================

#include "ui/TransportBar.h"

#include "i18n/Strings.h"
#include "ui/design/Icons.h"
#include "ui/design/MenuGlyph.h"

namespace dew
{

namespace
{

/** The metronome's right-click. One row, and an id rather than a bare 1 so a
    second row later has somewhere obvious to go. */
enum MetronomeItem
{
    countInItem = 1
};

} // namespace

void TransportBar::createToggles()
{
    metronomeButton.setComponentID ("metronome");
    metronomeButton.setClickingTogglesState (true);

    // The ENGINE's atomic is the truth, not this button and not the settings
    // file: the click can be turned on from here, from the Transport menu and
    // from a restored session, and one place that all three read and write is
    // what stops them drifting apart.
    metronomeButton.onClick = [this]
    { engine.setMetronomeEnabled (metronomeButton.getToggleState()); };

    metronomeButton.onContextMenu = [this] { showMetronomeMenu(); };
    addAndMakeVisible (metronomeButton);

    keyboardButton.setComponentID ("keyboardInput");
    keyboardButton.setClickingTogglesState (true);
    keyboardButton.onClick = [this]
    {
        if (onToggleKeyboardInput != nullptr)
            onToggleKeyboardInput();
    };
    addAndMakeVisible (keyboardButton);
}

void TransportBar::refreshToggles()
{
    // Latched, like every other poll on this bar: this runs at
    // motion::uiRefreshHz and setToggleState repaints.
    const auto clicking = engine.isMetronomeEnabled() ? 1 : 0;

    if (std::exchange (showingMetronome, clicking) != clicking)
        metronomeButton.setToggleState (clicking != 0, juce::dontSendNotification);

    if (isKeyboardInputEnabled == nullptr)
        return;

    const auto typing = isKeyboardInputEnabled() ? 1 : 0;

    if (std::exchange (showingKeyboardInput, typing) != typing)
        keyboardButton.setToggleState (typing != 0, juce::dontSendNotification);
}

juce::PopupMenu TransportBar::buildMetronomeMenu() const
{
    juce::PopupMenu menu;

    addGlyphItem (menu, MetronomeItem::countInItem, tr (StringId::transport_countIn_label),
                  icons::metronome(), true, countInEnabled);

    return menu;
}

void TransportBar::applyMetronomeChoice (int choice)
{
    if (choice == MetronomeItem::countInItem)
        countInEnabled = ! countInEnabled;
}

void TransportBar::showMetronomeMenu()
{
    auto menu = buildMetronomeMenu();
    menu.setLookAndFeel (&getLookAndFeel());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&metronomeButton),
                        [this] (int choice) { applyMetronomeChoice (choice); });
}

void TransportBar::applyMetronomeSettings (const Settings& settings)
{
    countInEnabled = settings.getCountInEnabled();

    engine.setMetronomeEnabled (settings.getMetronomeEnabled());

    // Straight to the button as well as through the poll, so the bar is right
    // in the first frame rather than in the first tick - a restored session
    // that flickers is a restored session somebody notices.
    showingMetronome = settings.getMetronomeEnabled() ? 1 : 0;
    metronomeButton.setToggleState (showingMetronome != 0, juce::dontSendNotification);
}

void TransportBar::captureMetronomeSettings (Settings& settings) const
{
    settings.setMetronomeEnabled (engine.isMetronomeEnabled());
    settings.setCountInEnabled (countInEnabled);
}

} // namespace dew
