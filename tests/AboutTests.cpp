#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/BuildInfo.h"
#include "ui/AboutPanel.h"
#include "ControlWalkHarness.h"
#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;

TEST_CASE ("about reports the build this binary is", "[ui][about]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AboutPanel panel;
    panel.setSize (AboutPanel::preferredWidth, AboutPanel::preferredHeight);
    panel.setVisible (true);
    panel.resized();

    // BuildInfo and nothing else, which is what makes this window incapable of
    // disagreeing with the version the application reports. BuildInfoTests holds
    // BuildInfo itself against the two lines in CMakeLists.txt.
    const auto build = panel.getBuildText();

    REQUIRE (build.contains (BuildInfo::version()));
    REQUIRE (build.contains (BuildInfo::name()));
    REQUIRE (build.contains (BuildInfo::jucePin().substring (0, 12)));

    REQUIRE (inkCoverage (render (panel)) > 0.0f);
}

TEST_CASE ("every control in the about window is named and reachable", "[ui][about][a11y]")
{
    // By hand, because forEachControl walks MainComponent and a dialog panel is
    // not in it - the same reason ConfirmPanel is not covered by it either.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AboutPanel panel;
    panel.setSize (AboutPanel::preferredWidth, AboutPanel::preferredHeight);
    panel.setVisible (true);
    panel.resized();

    auto controls = 0;

    walk (panel,
          [&] (juce::Component& c)
          {
              if (! isAControl (c))
                  return;

              ++controls;

              INFO (describe (c));

              if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (&c))
                  CHECK (client->getTooltip().isNotEmpty());

              // Title OR name, which is what AccessibilityTests asks of every
              // control in the window: juce::Button seeds its NAME from the
              // constructor argument, and that is a real accessible name.
              CHECK ((c.getTitle().isNotEmpty() || c.getName().isNotEmpty()));
              CHECK (c.getWantsKeyboardFocus());
              CHECK (! c.getBounds().isEmpty());
          });

    // The control case: a walk that found nothing would pass for the wrong
    // reason. Source and Close are the two.
    INFO (controls << " controls walked");
    REQUIRE (controls == 2);
}
