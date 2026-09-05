#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"
#include "ui/PreferencesCatalog.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"

namespace dew
{

/** One page of the preferences window.

    The base exists for the two things the window asks of a page without caring
    which kind it is: how tall it needs to be, and to mark the row a search
    result led to.
*/
class PreferencesPage : public juce::Component
{
public:
    /** Marks the row for `entry`, or clears the mark when null.

        A mark rather than a scroll-and-flash: motion is off in every test and
        every dew_shot render, so a highlight that faded would be a highlight
        neither could ever see. This one stays until the next result is chosen.
    */
    virtual void reveal (const prefs::Entry*) {}

    /** How tall the page's content is, which is what decides whether the
        window's viewport scrolls. */
    virtual int getRequiredHeight() const = 0;
};

// -----------------------------------------------------------------------------

/** One setting: what it is called, what it does, and the control that changes it.

    A component rather than a rectangle the page paints, which is what every
    other settings panel in dew does - because a row here has to be findable,
    markable and hit-testable on its own once search can point at one.
*/
class PreferencesRow : public juce::Component
{
public:
    /** @param control  laid out by this row and owned by the page, so a row
                        never has to know what kind of control it is holding. */
    PreferencesRow (const prefs::Entry&, juce::Component& control);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setRevealed (bool);

    const prefs::Entry& getEntry() const noexcept
    {
        return entry;
    }

    /** A title line and its explanation under it. A function rather than a
        constant so that nothing declares a dimension the token ladder owns. */
    static int height() noexcept;

private:
    const prefs::Entry& entry;
    juce::Component& control;
    bool revealed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesRow)
};

// -----------------------------------------------------------------------------

/** Theme, interface size, motion and language.

    Three of the four are COMMANDS, and this page invokes them rather than
    applying them: DewApplication::perform stays the only implementation, so the
    View menu and this page cannot come to disagree about what "high contrast"
    does. It reads the current value from where the application keeps it, not
    from the command manager, so the page is still right with no target
    registered - which is how dew_shot renders it.
*/
class AppearancePage : public PreferencesPage
{
public:
    /** @param commands  may be null, in which case the controls show the right
                         values and changing one does nothing. dew_shot has no
                         application to register a target with.
        @param onLanguageChosen  index 0 is "follow the system", the rest index
                         availableLocales(). The application's own chooseLanguage,
                         so the tag is stored and the notice shown in one place.
    */
    AppearancePage (Settings&, juce::ApplicationCommandManager* commands,
                    std::function<void (int)> onLanguageChosen);

    void resized() override;
    void reveal (const prefs::Entry*) override;
    int getRequiredHeight() const override;

private:
    void buildRows();

    Settings& settings;
    juce::ApplicationCommandManager* commands = nullptr;
    std::function<void (int)> onLanguageChosen;

    DewDropdown themeBox, uiScaleBox, motionBox, languageBox;
    std::vector<std::unique_ptr<PreferencesRow>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppearancePage)
};

// -----------------------------------------------------------------------------

/** What every render starts as, until the render dialog is told otherwise.

    The same five values the render dialog remembers, edited where they can be
    found rather than only where they are used. The lists come from
    ui/RenderChoices.h, so the two windows offer one set of formats and rates.
*/
class RenderingPage : public PreferencesPage
{
public:
    explicit RenderingPage (Settings&);

    void resized() override;
    void reveal (const prefs::Entry*) override;
    int getRequiredHeight() const override;

private:
    void buildRows();
    void store();

    Settings& settings;

    DewDropdown formatBox, rateBox, depthBox;
    DewNumberField tailField;
    DewCheckbox normalizeToggle;
    std::vector<std::unique_ptr<PreferencesRow>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderingPage)
};

// -----------------------------------------------------------------------------

/** A page that IS one of the panels dew already had.

    Audio, MIDI and MCP are not re-implemented here. The panel is embedded at
    its own preferred width, left-aligned rather than stretched, because it was
    laid out for that width and a device chooser spread across a wider pane
    reads as a different control.
*/
class HostedPage : public PreferencesPage
{
public:
    explicit HostedPage (std::unique_ptr<juce::Component> panel, int preferredWidth,
                         int preferredHeight);

    void resized() override;
    int getRequiredHeight() const override;

    /** The panel inside, so a test can reach the controls it is asserting on. */
    juce::Component* getPanel() const noexcept
    {
        return panel.get();
    }

private:
    std::unique_ptr<juce::Component> panel;
    int panelWidth = 0;
    int panelHeight = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedPage)
};

} // namespace dew
