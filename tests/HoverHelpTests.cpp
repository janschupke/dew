#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"
#include "engine/AudioEngine.h"
#include "io/LiveAudioHost.h"
#include "app/ProjectDocument.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/HoverHelp.h"
#include "ui/MainComponent.h"
#include "ui/design/Tokens.h"
#include "ui/StatusBar.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"

using namespace dew;

namespace
{

/** Where a control lives, as the chain of component IDs above it - so a failure
    names the panel to go and look at rather than a count. Lifted from
    ParamMenuTests, which needs the same sentence for the same reason.
*/
juce::String describe (juce::Component& c)
{
    juce::StringArray path;

    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (p->getComponentID().isNotEmpty())
            path.insert (0, p->getComponentID());

    if (path.isEmpty())
        path.add ("(no id)");

    return path.joinIntoString (" > ") + " @" + c.getBounds().toString();
}

/** Is this something a person clicks, drags or types into?

    By TYPE rather than by "does it handle the mouse", because the second
    question has no answer from outside a component - and because the list of
    things dew calls a control is exactly the list of primitives it built.
*/
bool isAControl (juce::Component& c)
{
    return dynamic_cast<DewButton*> (&c) != nullptr || dynamic_cast<DewIconButton*> (&c) != nullptr
           || dynamic_cast<DewLetterToggle*> (&c) != nullptr
           || dynamic_cast<DewKnob*> (&c) != nullptr
           || dynamic_cast<DewNumberField*> (&c) != nullptr
           || dynamic_cast<juce::ComboBox*> (&c) != nullptr;
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

TEST_CASE ("hover help reads the nearest tooltip above the pointer", "[ui][hover]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    juce::Component outer;
    outer.setComponentID ("outer");

    DewIconButton button { juce::Path(), "Play from the start" };
    outer.addAndMakeVisible (button);

    // Its own tooltip, when it has one.
    CHECK (HoverHelp::helpFor (button) == "Play from the start");

    // And the nearest one ABOVE it when it does not - which is the case that
    // matters: a DewKnob puts its tooltip on the juce::Slider inside it, and a
    // Label on a header row has none of its own.
    juce::Label inert;
    button.addAndMakeVisible (inert);
    CHECK (HoverHelp::helpFor (inert) == "Play from the start");

    // Nothing above it, nothing to say - rather than an empty string standing
    // in for a control that simply has no help.
    juce::Component orphan;
    CHECK (HoverHelp::helpFor (orphan).isEmpty());
}

TEST_CASE ("a hovered control reaches the status bar, and a message wins", "[ui][hover][statusbar]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    EditorState editorState;
    StatusBar bar { document, editorState, audioHost };

    bar.setSize (1200, tokens::size::stripStatus);
    bar.setVisible (true);
    bar.resized();

    bar.setHoverHelp ("Play from the start");
    CHECK (bar.getHoverHelp() == "Play from the start");

    // An error and "what that button does" are not comparable, and the message
    // already carries the severity rule that says so. The help is not LOST,
    // only outranked - it comes back when the message expires.
    bar.showMessage ("Could not open the audio device", StatusBar::Severity::error);
    REQUIRE (bar.hasMessage());
    CHECK (bar.getHoverHelp() == "Play from the start");

    bar.advanceMessageClock (StatusBar::messageLifetimeMs + 1);
    CHECK_FALSE (bar.hasMessage());
    CHECK (bar.getHoverHelp() == "Play from the start");
}

TEST_CASE ("every control in the window says what it is", "[ui][hover]")
{
    // The coverage guarantee, ENFORCED rather than promised. "A status-bar
    // explanation for every control" is the whole point of the feature, and a
    // control added to a panel with no help text is exactly the omission that
    // nobody notices until they are looking for it.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1400, 900);

    int controls = 0;
    juce::StringArray silent;

    // EVERY tab, not the one that happens to be in front: a TabbedComponent
    // parents only the current tab's content, so a walk of the window as it
    // opens covers the channel rack and none of the other four - which is a
    // gate that passes while saying almost nothing.
    for (int tab = 0; tab < Settings::numTabs; ++tab)
    {
        component.showTab (tab);
        component.resized();

        walk (component,
              [&] (juce::Component& c)
              {
                  if (! isAControl (c))
                      return;

                  ++controls;

                  if (HoverHelp::helpFor (c).isEmpty())
                      silent.addIfNotAlreadyThere (describe (c));
              });
    }

    // A control case: a walk that found nothing would pass for the wrong reason,
    // and so would one that only ever saw a single tab.
    INFO (controls << " controls, " << (controls - silent.size()) << " with help");
    REQUIRE (controls > 100);

    INFO ("controls with nothing to say:\n" << silent.joinIntoString ("\n"));
    CHECK (silent.isEmpty());
}
