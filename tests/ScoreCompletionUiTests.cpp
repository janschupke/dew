// Control-Space, and what the popup offers.
//
// Split out of ScoreEditorTests.cpp, along the Catch2 tags it already
// carried. The fixture is ScoreEditorHarness.h.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "io/OfflineRenderer.h"
#include "lang/Compile.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ScoreBake.h"
#include "ui/ScoreEditorComponent.h"
#include "ui/design/Tokens.h"

#include "FixtureProject.h"
#include "PaintProbe.h"
#include "ScoreEditorHarness.h"

using namespace dew;
using namespace dew::testing;

TEST_CASE ("completion offers, filters and inserts", "[score][editor][completion]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    editor.getSourceDocument().replaceAllContent ("cha");
    editor.getEditor().moveCaretTo (juce::CodeDocument::Position (editor.getSourceDocument(), 3),
                                    false);

    REQUIRE_FALSE (editor.isCompletionVisible());

    editor.showCompletions();

    REQUIRE (editor.isCompletionVisible());
    REQUIRE (editor.getCompletionList().getItems().size() == 1);
    REQUIRE (editor.getCompletionList().getItems().front().text == "channel");

    editor.acceptCompletion();

    // Replaced, not appended: `cha` + `channel` would be `chachannel`.
    REQUIRE (editor.getSourceDocument().getAllContent() == "channel");
    REQUIRE (editor.getEditor().getCaretPos().getPosition() == 7);
    REQUIRE_FALSE (editor.isCompletionVisible());
}

TEST_CASE ("the completion popup is a component, not a menu", "[score][editor][completion]")
{
    // A juce::PopupMenu is modal, which a headless test cannot drive, and its
    // MenuItemIterator holds a reference to a menu that may already be gone.
    // This one can be found by id, painted offscreen and driven with real keys.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    editor.getSourceDocument().replaceAllContent ("song {\n  \n}\n");
    editor.getEditor().moveCaretTo (juce::CodeDocument::Position (editor.getSourceDocument(), 9),
                                    false);
    editor.showCompletions();

    REQUIRE (editor.isCompletionVisible());

    auto& popup = editor.getCompletionList();
    REQUIRE (popup.getComponentID() == "scoreCompletion");

    // On screen, inside the editor, and not a zero-sized rectangle nobody sees.
    REQUIRE (popup.getWidth() > 0);
    REQUIRE (popup.getHeight() > 0);
    REQUIRE (editor.getLocalBounds().contains (popup.getBounds()));

    // It draws its candidates.
    const auto image = render (popup);
    REQUIRE (coverageOf (image, tokens::colour::accent) > 0.0f);
}

TEST_CASE ("the popup owns its keys while it is open", "[score][editor][completion]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    editor.getSourceDocument().replaceAllContent ("song {\n  \n}\n");
    editor.getEditor().moveCaretTo (juce::CodeDocument::Position (editor.getSourceDocument(), 9),
                                    false);
    editor.showCompletions();

    auto& popup = editor.getCompletionList();
    REQUIRE (popup.getItems().size() > 1);
    REQUIRE (popup.getSelected()->text == popup.getItems().front().text);

    popup.moveSelection (1);
    REQUIRE (popup.getSelected()->text == popup.getItems()[1].text);

    // Wraps rather than sticking, so walking the list never means looking at
    // where the selection went instead of at the code.
    popup.moveSelection (-1);
    popup.moveSelection (-1);
    REQUIRE (popup.getSelected()->text == popup.getItems().back().text);

    editor.hideCompletions();
    REQUIRE_FALSE (editor.isCompletionVisible());

    // And the document was never touched by any of it.
    REQUIRE (editor.getSourceDocument().getAllContent() == "song {\n  \n}\n");
}

TEST_CASE ("completion knows which block the caret is in", "[score][editor][completion]")
{
    // The end-to-end version of what ScoreCompletionTests proves about the pure
    // function: the caret position the EDITOR reports has to reach it as the
    // byte offset the compiler means.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    const juce::String source = "channel pad {\n  \n}\n";
    editor.getSourceDocument().replaceAllContent (source);
    editor.getEditor().moveCaretTo (juce::CodeDocument::Position (editor.getSourceDocument(), 16),
                                    false);

    editor.showCompletions();
    REQUIRE (editor.isCompletionVisible());

    std::vector<std::string> texts;

    for (const auto& item : editor.getCompletionList().getItems())
        texts.push_back (item.text);

    REQUIRE (std::find (texts.begin(), texts.end(), "mixer") != texts.end());
    REQUIRE (std::find (texts.begin(), texts.end(), "tempo") == texts.end());
}

TEST_CASE ("a caret after a multi-byte character still completes the right thing",
           "[score][editor][completion]")
{
    // The byte-versus-character divergence, from the other direction: the caret
    // is a CHARACTER index and completion is asked in BYTES, so one em dash
    // above the caret would otherwise ask about the wrong place entirely.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const std::string source = "// \xe2\x80\x94 a note\nchannel pad {\n  mix\n}\n";

    REQUIRE (ScoreEditorComponent::byteIndexForCharacter (source, 0) == 0);

    // The comment is 12 characters and 14 bytes, so everything after it is out
    // of step by two.
    REQUIRE (ScoreEditorComponent::byteIndexForCharacter (source, 12) == 14);

    // And it is exactly the inverse of the conversion the squiggles use.
    for (auto character = 0; character < 20; ++character)
    {
        const auto bytes = ScoreEditorComponent::byteIndexForCharacter (source, character);
        INFO ("character " << character << " -> byte " << bytes);
        REQUIRE (ScoreEditorComponent::characterIndexForByte (source, (std::uint32_t) bytes)
                 == character);
    }
}
