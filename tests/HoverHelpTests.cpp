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
#include "ui/primitives/DewButtons.h"
#include "ui/primitives/DewKnob.h"
#include "ui/primitives/DewNumberField.h"
#include "ControlWalkHarness.h"

using namespace dew;
using namespace dew::testing;

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

    juce::StringArray silent;

    const auto controls = forEachControl (component,
                                          [&] (juce::Component& c)
                                          {
                                              if (HoverHelp::helpFor (c).isEmpty())
                                                  silent.addIfNotAlreadyThere (describe (c));
                                          });

    // A control case: a walk that found nothing would pass for the wrong reason,
    // and so would one that only ever saw a single tab.
    INFO (controls << " controls, " << (controls - silent.size()) << " with help");
    REQUIRE (controls > 100);

    INFO ("controls with nothing to say:\n" << silent.joinIntoString ("\n"));
    CHECK (silent.isEmpty());
}
