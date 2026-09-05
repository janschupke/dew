// =============================================================================
// The three settings that are commands: theme, motion and interface size.
//
// The same class, a third translation unit beside DewApplication.cpp and
// DewApplicationMenus.cpp.
//
// Together here because they are now reached two ways. The View menu invokes
// them and so does the Appearance page of the preferences window, which means
// this file is the ONE implementation of what each does - the window does not
// apply a theme, it asks for the same command the menu asks for. Split out when
// the second caller appeared, which is also when DewApplication.cpp ran out of
// room under the 400-code-line gate.
//
// Each is a contiguous run of command ids in the same order as the thing it
// selects, so every one of these is an index rather than a switch.
// =============================================================================

#include "DewApplication.h"

#include "ui/Hotkeys.h"
#include "ui/design/Animator.h"
#include "ui/design/SystemMotionPreference.h"
#include "ui/design/Theme.h"

namespace dew
{

void DewApplication::tickViewPreference (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    // Ticked rather than merely listed: four items that all read as available
    // say nothing about which one you are looking at.
    info.setActive (settings != nullptr);

    if (settings == nullptr)
        return;

    if (id >= CommandIDs::viewUiScaleFirst && id <= CommandIDs::viewUiScale175)
    {
        const auto step = (int) (id - CommandIDs::viewUiScaleFirst);

        info.setTicked (
            step >= 0 && step < Settings::numUiScaleSteps
            && juce::approximatelyEqual (settings->getUiScale(), Settings::uiScaleSteps[step]));
        return;
    }

    if (id >= CommandIDs::viewMotionFirst && id <= CommandIDs::viewMotionReduced)
    {
        info.setTicked ((int) settings->getMotionPreference()
                        == (int) (id - CommandIDs::viewMotionFirst));
        return;
    }

    // The theme is asked of the palette in force rather than of the store: they
    // agree, and the one that is true right now is the one a tick is about.
    info.setTicked ((int) theme::current() == (int) (id - CommandIDs::viewThemeFirst));
}

bool DewApplication::applyViewPreference (juce::CommandID id)
{
    if (settings == nullptr)
        return false;

    if (id >= CommandIDs::viewUiScaleFirst && id <= CommandIDs::viewUiScale175)
    {
        const auto step = (int) (id - CommandIDs::viewUiScaleFirst);

        if (step < 0 || step >= Settings::numUiScaleSteps)
            return false;

        settings->setUiScale (Settings::uiScaleSteps[step]);
        applyUiScale (Settings::uiScaleSteps[step]);
        commandManager.commandStatusChanged();
        return true;
    }

    if (id >= CommandIDs::viewMotionFirst && id <= CommandIDs::viewMotionReduced)
    {
        const auto step = (int) (id - CommandIDs::viewMotionFirst);

        if (step < 0 || step > (int) Settings::Motion::reduced)
            return false;

        settings->setMotionPreference ((Settings::Motion) step);

        // The OS answer is passed IN, because reading it is per-OS code that
        // lives in dew_design and dew_app sits beside dew_design rather than
        // under it.
        Animator::shared().setReduceMotion (
            settings->getReduceMotion (systemPrefersReducedMotion()));
        commandManager.commandStatusChanged();
        return true;
    }

    const auto kind = id == CommandIDs::viewThemeHighContrast ? theme::Kind::highContrast
                                                              : theme::Kind::dark;

    settings->setThemeName (theme::name (kind));

    // Applied to the window rather than to the palette alone: theme::apply
    // re-seeds the look and feel AND sends lookAndFeelChanged, which is what
    // reaches everything that copied a colour when it was built. Anything open
    // over the window - the preferences panel that may have asked for this - is
    // a child of the desktop rather than of this, so it takes its own.
    if (auto* main = getMainComponent())
        theme::apply (kind, *main);

    commandManager.commandStatusChanged();
    return true;
}

} // namespace dew
