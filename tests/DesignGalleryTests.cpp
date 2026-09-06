// The gallery page, which is every control at once.
//
// Split out of DesignSystemTests.cpp along the tags it already carried. It had
// no shared fixture to divide - its anonymous namespace was empty.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "ui/design/DewGallery.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Icons.h"
#include "ui/design/SignalScope.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/ButtonBehaviour.h"
#include "ui/primitives/DewNumberField.h"
#include "ui/primitives/HoverTracker.h"

#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;

TEST_CASE ("the gallery lays out and paints every section", "[design][gallery]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewGallery gallery;
    gallery.setVisible (true);
    gallery.setSize (1200, 400); // deliberately too short
    gallery.setSize (1200, gallery.getRequiredHeight());

    REQUIRE (gallery.getRequiredHeight() > 400);

    const auto image = render (gallery);
    REQUIRE (inkCoverage (image) > 0.05f);

    // The last section must actually be inside the reported height: a gallery
    // that clips its own content is worse than none.
    const auto bottomStrip = image.getClippedImage (
        { 0, gallery.getRequiredHeight() - 120, image.getWidth(), 100 });

    REQUIRE (inkCoverage (bottomStrip) > 0.0f);
}

TEST_CASE ("every control on the gallery page is given bounds", "[design][gallery]")
{
    // The page walks its controls positionally with a hard-coded count per
    // section, so a control added to the constructor without a matching entry
    // in the layout shifts every control after it into someone else's slot. In
    // a debug build the assertion in layOut catches that; this is the version
    // that runs in the build CI uses. A control nobody placed keeps an empty
    // rectangle, which is exactly what this looks for.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewGallery gallery;
    gallery.setSize (1440, 400);
    gallery.setSize (1440, gallery.getRequiredHeight());

    REQUIRE (gallery.getNumChildComponents() > 0);

    for (auto* child : gallery.getChildren())
    {
        INFO ("child " << gallery.getIndexOfChildComponent (child));
        REQUIRE (! child->getBounds().isEmpty());
    }
}

TEST_CASE ("the gallery shows the signal scope empty and with a signal", "[design][gallery]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewGallery gallery;
    gallery.setSize (1440, 400);
    gallery.setSize (1440, gallery.getRequiredHeight());

    int idle = 0, driven = 0;

    for (auto* child : gallery.getChildren())
        if (auto* scope = dynamic_cast<SignalScope*> (child))
            (scope->isIdle() ? idle : driven) += 1;

    // Both states on the page, so the PNG documents what "nothing is sounding"
    // looks like as well as what a signal does.
    REQUIRE (idle == 1);
    REQUIRE (driven == 1);
}
