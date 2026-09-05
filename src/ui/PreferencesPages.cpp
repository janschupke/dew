// =============================================================================
// The rows, the pages built from the catalog, and the wrapper for a page that
// is one of dew's existing panels.
//
// RenderingPage is next door in PreferencesRendering.cpp, for the reason
// MainComponent is spread over three files: no file here is over 400 lines of
// code, and a gate says so.
// =============================================================================

#include "ui/PreferencesPages.h"

#include <iterator>

#include "i18n/Strings.h"
#include "ui/Hotkeys.h"
#include "ui/design/Theme.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace
{

/** The control column. Wide enough for "Follow the system" at body size and
    narrow enough to leave the title room, which is what decides it - it is not
    a rung of the size ladder and must not be mistaken for one.
*/
constexpr int controlColumnWidth = 200;

} // namespace

// --- a row -------------------------------------------------------------------

PreferencesRow::PreferencesRow (const prefs::Entry& e, juce::Component& c)
    : entry (e)
    , control (c)
{
    setTitle (tr (entry.title));

    // PARENTED here, not merely positioned. resized() lays the control out in
    // this row's coordinates, so a control still parented by the page would be
    // given a rectangle measured from the row and applied from the page - which
    // put all four of the appearance controls on top of each other at the top
    // of it, with only the last one laid out visible.
    //
    // The page still OWNS it; a juce::Component parent does not.
    addAndMakeVisible (control);
}

int PreferencesRow::descriptionHeight() noexcept
{
    return (int) type::small + space::xs;
}

int PreferencesRow::height() noexcept
{
    // The title shares the control's row, so the two sit on one centre line;
    // the description is directly under it in a box the size of its own line,
    // so the pair reads as one row rather than as two. It was
    // controlHeight + controlHeightSm + space::xs, of which the last four
    // pixels were never drawn into at all.
    return size::controlHeight + descriptionHeight();
}

void PreferencesRow::setRevealed (bool shouldBeRevealed)
{
    if (revealed == shouldBeRevealed)
        return;

    revealed = shouldBeRevealed;
    repaint();
}

void PreferencesRow::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    // A bar down the left edge rather than a wash behind the row: a filled
    // background would have to clear the contrast bar against everything drawn
    // on top of it, and this says the same thing against any of them.
    if (revealed)
    {
        g.setColour (colour::accent);
        g.fillRect (area.removeFromLeft (stroke::hairlinePx * 2));
        area.removeFromLeft (space::sm);
    }

    auto title = area.removeFromTop (size::controlHeight);
    title.removeFromRight (controlColumnWidth + space::lg);

    g.setColour (colour::textPrimary);
    g.setFont (type::font (type::body));
    g.drawFittedText (tr (entry.title), title, juce::Justification::centredLeft, 1);

    // type::small rather than type::caption: eleven pixels is the size the
    // ruler numbers and the knob captions are, and a sentence explaining a
    // setting is read rather than glanced at.
    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::small));
    g.drawFittedText (tr (entry.description), area.removeFromTop (descriptionHeight()),
                      juce::Justification::centredLeft, 1);
}

void PreferencesRow::resized()
{
    auto area = getLocalBounds().removeFromTop (size::controlHeight);
    control.setBounds (area.removeFromRight (controlColumnWidth));
}

// --- a stack of rows ---------------------------------------------------------

namespace prefs
{

void stackRows (const std::vector<std::unique_ptr<PreferencesRow>>& rows, juce::Rectangle<int> area)
{
    for (const auto& row : rows)
    {
        row->setBounds (area.removeFromTop (PreferencesRow::height()));
        area.removeFromTop (space::lg);
    }
}

int stackHeight (int count)
{
    // Gaps BETWEEN rows, which is one fewer than there are rows. Both pages
    // multiplied the gap by the row count, and the trailing one asked the
    // viewport for height nothing was ever drawn into.
    return count <= 0 ? 0 : count * PreferencesRow::height() + (count - 1) * space::lg;
}

} // namespace prefs

// --- appearance --------------------------------------------------------------

AppearancePage::AppearancePage (Settings& s, juce::ApplicationCommandManager* c,
                                std::function<void (int)> chooseLanguage)
    : settings (s)
    , commands (c)
    , onLanguageChosen (std::move (chooseLanguage))
{
    setComponentID ("preferencesAppearance");

    // The names come from the command registry rather than from a second list,
    // so a menu item renamed once is renamed here too.
    const auto nameOf = [] (juce::CommandID id)
    {
        const auto* binding = hotkeys::find (id);
        return binding != nullptr ? tr (binding->name) : juce::String();
    };

    for (auto step = 0; step < 2; ++step)
        themeBox.addItem (nameOf (CommandIDs::viewThemeFirst + step), step + 1);

    themeBox.setSelectedId ((int) theme::current() + 1, juce::dontSendNotification);

    for (auto step = 0; step < Settings::numUiScaleSteps; ++step)
        uiScaleBox.addItem (nameOf (CommandIDs::viewUiScaleFirst + step), step + 1);

    for (auto step = 0; step < Settings::numUiScaleSteps; ++step)
        if (juce::approximatelyEqual (settings.getUiScale(), Settings::uiScaleSteps[step]))
            uiScaleBox.setSelectedId (step + 1, juce::dontSendNotification);

    for (auto step = 0; step < 3; ++step)
        motionBox.addItem (nameOf (CommandIDs::viewMotionFirst + step), step + 1);

    motionBox.setSelectedId ((int) settings.getMotionPreference() + 1, juce::dontSendNotification);

    // Data-driven, the way the View menu's language submenu is: a locale is a
    // JSON file and a word in CMake, and it appears here without an edit.
    const auto tags = availableLocales();
    const auto chosen = settings.getLanguage();

    languageBox.addItem (tr (StringId::menu_languageSystem), 1);

    for (auto i = 0; i < tags.size(); ++i)
        languageBox.addItem (endonymOf (tags[i]), i + 2);

    languageBox.setSelectedId (chosen.isEmpty() ? 1 : tags.indexOf (chosen) + 2,
                               juce::dontSendNotification);

    // Invoked rather than applied. The command is the implementation; this is a
    // second way to reach it, not a second copy of it.
    const auto invoke = [this] (juce::CommandID first, int index)
    {
        if (commands != nullptr && index > 0)
            commands->invokeDirectly (first + index - 1, false);
    };

    themeBox.onChange = [this, invoke]
    { invoke (CommandIDs::viewThemeFirst, themeBox.getSelectedId()); };
    uiScaleBox.onChange = [this, invoke]
    { invoke (CommandIDs::viewUiScaleFirst, uiScaleBox.getSelectedId()); };
    motionBox.onChange = [this, invoke]
    { invoke (CommandIDs::viewMotionFirst, motionBox.getSelectedId()); };

    languageBox.onChange = [this]
    {
        if (onLanguageChosen != nullptr && languageBox.getSelectedId() > 0)
            onLanguageChosen (languageBox.getSelectedId() - 1);
    };

    buildRows();
}

void AppearancePage::buildRows()
{
    // Typed as what they are, so setTooltip is the DewDropdown override that
    // sets the accessible name too rather than Component's, which does not.
    DewDropdown* controls[] { &themeBox, &uiScaleBox, &motionBox, &languageBox };
    auto next = 0;

    for (const auto& entry : prefs::entries())
    {
        if (entry.page != prefs::Page::appearance)
            continue;

        // The catalog's order IS the page's order, so a row added there without
        // a control here would read the array off its end.
        jassert (next < (int) std::size (controls));

        auto* control = controls[next++];

        control->setWantsKeyboardFocus (true);
        control->setTooltip (tr (entry.description));

        // The row parents it - see PreferencesRow's constructor.
        auto row = std::make_unique<PreferencesRow> (entry, *control);
        addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
}

void AppearancePage::resized()
{
    prefs::stackRows (rows, getLocalBounds());
}

void AppearancePage::reveal (const prefs::Entry* entry)
{
    for (const auto& row : rows)
        row->setRevealed (entry != nullptr && &row->getEntry() == entry);
}

int AppearancePage::getRequiredHeight() const
{
    return prefs::stackHeight ((int) rows.size());
}

// --- a page that is a panel dew already had -----------------------------------

HostedPage::HostedPage (std::unique_ptr<dialog::Panel> p, int standaloneHeight)
    : panel (std::move (p))
    , panelHeight (standaloneHeight)
{
    setComponentID ("preferencesHosted");

    if (panel != nullptr)
    {
        // The pane around it has already applied the inset every dew dialog
        // applies, and painted the ground under it. A second inset would put
        // this panel's controls 32 pixels from an edge every other panel in the
        // application sits 16 from.
        panel->setEmbedded (true);
        addAndMakeVisible (*panel);
    }
}

void HostedPage::resized()
{
    if (panel != nullptr)
        panel->setBounds (getLocalBounds().withHeight (getRequiredHeight()));
}

int HostedPage::getRequiredHeight() const
{
    // The height it asks for as a dialog, less the inset it no longer applies.
    return panelHeight - space::xl * 2;
}

} // namespace dew
