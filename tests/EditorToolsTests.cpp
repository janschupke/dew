// The tool register the two timeline strips share.
//
// Its own file because the thing under test is neither toolbar: it is the
// register they both build out of, and a case that reached for a whole
// PianoRollToolbar to ask what the slice key does would be testing the strip
// around it rather than the rule.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/EditorTools.h"
#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"

using namespace dew;

namespace
{

/** A strip's owner. ToolStrip parents its buttons, so there has to be one. */
struct ToolHost : juce::Component
{
};

std::vector<ToolStrip::Entry> twoTools()
{
    return { { EditorTool::select, icons::pointer(), "Select" },
             { EditorTool::paint, icons::pencil(), "Paint" } };
}

std::vector<ToolStrip::Entry> threeTools()
{
    auto rows = twoTools();
    rows.push_back ({ EditorTool::slice, icons::scissors(), "Slice" });
    return rows;
}

} // namespace

TEST_CASE ("a strip offers the tools it was given and no others", "[ui][tools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ToolHost host;
    ToolStrip tools { host, twoTools() };

    CHECK (tools.offers (EditorTool::select));
    CHECK (tools.offers (EditorTool::paint));

    // The playlist's case. A view that has no slice tool has to be able to say
    // so, rather than every caller knowing which views have which.
    CHECK_FALSE (tools.offers (EditorTool::slice));

    // A button per tool, and nothing else parented.
    CHECK (host.getNumChildComponents() == 2);
}

TEST_CASE ("a tool key a strip has no tool for is refused rather than obeyed", "[ui][tools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ToolHost host;
    ToolStrip tools { host, twoTools() };

    REQUIRE (tools.getTool() == EditorTool::select);

    CHECK (tools.applyCommand (hotkeys::ViewCommand::paintTool));
    CHECK (tools.getTool() == EditorTool::paint);

    // Answered false AND left alone. The playlist used to have no case for this
    // command at all, so the key was neither handled nor passed on - which is a
    // key that does nothing for a reason no reader of that file could see.
    CHECK_FALSE (tools.applyCommand (hotkeys::ViewCommand::eraseTool));
    CHECK (tools.getTool() == EditorTool::paint);

    // Not a tool command at all.
    CHECK_FALSE (tools.applyCommand (hotkeys::ViewCommand::zoomToFit));
    CHECK (tools.getTool() == EditorTool::paint);

    // ...and the roll's strip, which does have it.
    ToolHost rollHost;
    ToolStrip roll { rollHost, threeTools() };

    CHECK (roll.applyCommand (hotkeys::ViewCommand::eraseTool));
    CHECK (roll.getTool() == EditorTool::slice);
}

TEST_CASE ("exactly one tool button is lit, whatever happens to the radio group", "[ui][tools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ToolHost host;
    ToolStrip tools { host, threeTools() };

    const auto lit = [&host]
    {
        int count = 0;

        for (auto* child : host.getChildren())
            if (auto* button = dynamic_cast<juce::Button*> (child); button->getToggleState())
                ++count;

        return count;
    };

    CHECK (lit() == 1);

    tools.setTool (EditorTool::slice);
    CHECK (lit() == 1);

    // Setting the tool that is already current is the case the radio group gets
    // wrong on its own: clicking a lit button in a group clears it, and the
    // strip would be left showing no tool at all.
    tools.setTool (EditorTool::slice);
    CHECK (lit() == 1);

    // A tool this strip does not have changes nothing, including the lighting.
    ToolStrip two { host, twoTools() };
    two.setTool (EditorTool::slice);
    CHECK (two.getTool() == EditorTool::select);
}

TEST_CASE ("a strip asks for the width its own buttons need", "[ui][tools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ToolHost host;
    ToolStrip two { host, twoTools() };
    ToolStrip three { host, threeTools() };

    // Derived rather than a number kept in step by hand, which is what both
    // strips did: a toolbar that under-reports its width never shows its
    // overflow button and silently drops the controls that did not fit.
    CHECK (three.preferredWidth()
           == two.preferredWidth() + tokens::size::iconButton + tokens::space::xxs);
}

TEST_CASE ("only the tool that changed is announced", "[ui][tools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ToolHost host;
    ToolStrip tools { host, threeTools() };

    auto announced = 0;
    tools.onToolChanged = [&announced] { ++announced; };

    tools.setTool (EditorTool::paint);
    CHECK (announced == 1);

    tools.setTool (EditorTool::paint);
    CHECK (announced == 1);

    tools.setTool (EditorTool::select, juce::dontSendNotification);
    CHECK (announced == 1);
    CHECK (tools.getTool() == EditorTool::select);
}
