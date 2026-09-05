#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/DewDialog.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** What this binary is, and what it is built from.

    dew had no About of any kind: BuildInfo::summary() existed, said exactly the
    right thing - the version and the JUCE commit behind it - and had no caller
    in src/ at all, so the running application could not answer the first
    question anybody asks of one.

    It reads NO file. THIRD_PARTY.md and both licences are staged into the
    bundle by cmake/Packaging.cmake and are named here, but opening one would be
    file I/O from dew_ui for a paragraph that is already compiled in. What is
    shown is what the build definitions carry.

    The licence line is not decoration. dew is AGPLv3, which obliges an offer of
    the source to whoever holds the binary, and the button beside it is that
    offer being kept.
*/
class AboutPanel : public dialog::Panel
{
public:
    AboutPanel();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens it over `parent`, with dew's dialog conventions. */
    static void show (juce::Component* parent);

    // --- for tests -----------------------------------------------------------
    /** The provenance line, which is BuildInfo::summary() and nothing else - so
        a test can hold the window against the two version lines in
        CMakeLists.txt without rendering it.

        Drawn rather than put on a control, so it carries no tooltip: it is a
        statement, not something you do anything to. */
    juce::String getBuildText() const;

    juce::Button& getSourceButton() noexcept
    {
        return sourceButton;
    }
    juce::Button& getCloseButton() noexcept
    {
        return closeButton;
    }

    static constexpr int preferredWidth = 380;

    /** The name, the build line, two paragraphs and a button row. */
    static constexpr int preferredHeight = 260;

private:
    DewButton sourceButton { {}, DewButton::Role::ghost };
    DewButton closeButton { {}, DewButton::Role::primary };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutPanel)
};

} // namespace dew
