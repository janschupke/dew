#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "io/OfflineRenderer.h"
#include "lang/Compile.h"
#include "lang/Lexer.h"
#include "lang/ScanCore.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ScoreBake.h"
#include "ui/ScoreEditorComponent.h"
#include "ui/ScoreTokeniser.h"
#include "ui/design/Tokens.h"
#include "PaintProbe.h"
#include "FixtureProject.h"

using namespace dew;
using namespace dew::testing;

namespace
{

std::string exampleSource()
{
    const juce::File file { juce::String (DEW_EXAMPLES_DIR) + "/amber.score" };
    REQUIRE (file.existsAsFile());
    return file.loadFileAsString().toStdString();
}

/** A short score that compiles, so a test can vary one line of it. */
std::string workingSource()
{
    return "song {\n"
           "  tempo 120\n"
           "  meter 4/4\n"
           "  key   C major\n"
           "}\n"
           "channel pad { mixer 1 }\n"
           "voicing warm { size 3 voices }\n"
           "rhythm held { 1/1 }\n"
           "harmony h { I | vi | IV | V }\n"
           "section verse {\n"
           "  length 4 bars\n"
           "  harmony h\n"
           "  part pad {\n"
           "    chords with warm\n"
           "    rhythm held\n"
           "  }\n"
           "}\n"
           "arrangement {\n  verse\n}\n";
}

/** A score needing a finer grid than a project starts with: a sixteenth needs
    four steps a beat and an eighth-note triplet needs three, so together they
    need twelve. amber.score, for all its length, only ever asks for four.
*/
std::string finerGridSource()
{
    return "song {\n"
           "  tempo 120\n"
           "  meter 4/4\n"
           "  key   C major\n"
           "}\n"
           "channel lead {\n  mixer 1\n  range C4..C6\n}\n"
           "rhythm swung { 1/16 1/16 1/8t 1/8t 1/8t }\n"
           "harmony h { I | vi | IV | V }\n"
           "section verse {\n  length 4 bars\n  harmony h\n"
           "  part lead {\n    melody {\n      rhythm swung\n    }\n  }\n}\n"
           "arrangement {\n  verse\n}\n";
}

/** The kinds the EDITOR's cursor produces, walking a CodeDocument. */
std::vector<lang::TokenKind> kindsThroughTheEditor (const juce::String& text)
{
    juce::CodeDocument document;
    document.replaceAllContent (text);

    juce::CodeDocument::Iterator iterator { document };
    std::vector<lang::TokenKind> kinds;

    while (! iterator.isEOF())
    {
        CodeDocumentCursor cursor { iterator };
        lang::skipSpace (cursor);

        if (cursor.isEOF())
            break;

        kinds.push_back (lang::scanOne (cursor));
    }

    return kinds;
}

/** The kinds the COMPILER produces, walking the same text as bytes. */
std::vector<lang::TokenKind> kindsThroughTheCompiler (const std::string& text)
{
    std::vector<lang::TokenKind> kinds;

    for (const auto& token : lang::tokenize (text))
        if (token.kind != lang::TokenKind::endOfFile)
            kinds.push_back (token.kind);

    return kinds;
}

} // namespace

TEST_CASE ("the editor and the compiler read the same tokens", "[score][editor]")
{
    // THE invariant this design exists for. The editor's tokeniser and the
    // compiler's lexer are two instantiations of one template over one
    // classifier, so there is no second, approximate copy of the grammar living
    // in the highlighter. If these ever disagree, the colours are lying about
    // what the compiler will do with the text.
    const auto source = exampleSource();
    const auto throughEditor = kindsThroughTheEditor (juce::String (source));
    const auto throughCompiler = kindsThroughTheCompiler (source);

    REQUIRE_FALSE (throughCompiler.empty());

    REQUIRE (throughEditor.size() == throughCompiler.size());

    for (std::size_t i = 0; i < throughCompiler.size(); ++i)
    {
        INFO ("token " << i << ": editor " << lang::nameOf (throughEditor[i]) << ", compiler "
                       << lang::nameOf (throughCompiler[i]));
        REQUIRE (throughEditor[i] == throughCompiler[i]);
    }
}

TEST_CASE ("a keyword is coloured because the schema declares it", "[score][editor]")
{
    // Not from a hand-written keyword list. A second list would be a second
    // grammar, and it would be wrong the first time somebody added a key.
    REQUIRE (ScoreTokeniser::isSchemaWord ("song"));
    REQUIRE (ScoreTokeniser::isSchemaWord ("harmony"));
    REQUIRE (ScoreTokeniser::isSchemaWord ("variance"));

    // A name the user chose is not one.
    REQUIRE_FALSE (ScoreTokeniser::isSchemaWord ("verse"));
    REQUIRE_FALSE (ScoreTokeniser::isSchemaWord ("lament"));

    REQUIRE (ScoreTokeniser::colourFor (lang::TokenKind::word, "song") == ScoreTokeniser::keyword);
    REQUIRE (ScoreTokeniser::colourFor (lang::TokenKind::word, "verse") == ScoreTokeniser::plain);
    REQUIRE (ScoreTokeniser::colourFor (lang::TokenKind::ratio, "1/8t") == ScoreTokeniser::literal);
    REQUIRE (ScoreTokeniser::colourFor (lang::TokenKind::unknown, "\\") == ScoreTokeniser::invalid);
}

TEST_CASE ("a byte offset becomes the character index the editor means", "[score][editor]")
{
    // The compiler counts bytes, which is right for a caret printed under a
    // line; CodeDocument::Position counts characters. One em dash in a comment
    // puts every squiggle after it two columns too far right.
    const std::string ascii = "song {";
    REQUIRE (ScoreEditorComponent::characterIndexForByte (ascii, 5) == 5);

    // "// \xe2\x80\x94" is four characters and six bytes.
    const std::string emDash = "// \xe2\x80\x94 tempo";
    REQUIRE (ScoreEditorComponent::characterIndexForByte (emDash, 3) == 3);
    REQUIRE (ScoreEditorComponent::characterIndexForByte (emDash, 6) == 4);
    REQUIRE (ScoreEditorComponent::characterIndexForByte (emDash, 12) == 10);

    // Past the end clamps rather than reading off it.
    REQUIRE (ScoreEditorComponent::characterIndexForByte (ascii, 9999) == 6);
}

TEST_CASE ("the score tab shows what the project holds", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ProjectEdits::setScoreSource (document.getState(), juce::String (workingSource()), "t.score",
                                  nullptr);

    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    // refresh() is what File > Open reaches, and it has to pick the text up.
    editor.refresh();

    REQUIRE (editor.getSourceDocument().getAllContent().toStdString() == workingSource());
    REQUIRE (editor.getDiagnostics().empty());
}

TEST_CASE ("typing is checked once, after it stops", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    const auto before = editor.getCheckCount();

    // Ten edits in a row. A check per keystroke would compile the file ten
    // times and, worse, would put ten undo entries on the project.
    for (auto i = 0; i < 10; ++i)
        editor.getSourceDocument().insertText (0, "x");

    REQUIRE (editor.getCheckCount() == before);
    REQUIRE (editor.isCheckPending());

    // JUCE_MODAL_LOOPS_PERMITTED is 0, so a test cannot run a dispatch loop to
    // let the Timer fire. What is under test is ours anyway: that ten edits
    // coalesced into ONE pending check rather than ten.
    editor.flushPendingCheck();

    INFO ("checks: " << editor.getCheckCount() - before);
    REQUIRE (editor.getCheckCount() == before + 1);
    REQUIRE_FALSE (editor.isCheckPending());
}

TEST_CASE ("typing a score stores it in the project without compiling it", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    editor.getSourceDocument().replaceAllContent (juce::String (workingSource()));
    editor.flushPendingCheck();

    // The text is saved...
    REQUIRE (ProjectEdits::scoreSource (document.getState()).toStdString() == workingSource());

    // ...and NOT a single note was written. A debounced auto-compile would put
    // an undo step full of notes on every pause in typing and would replace
    // hand edits without being asked.
    for (const auto& child : document.getState())
        if (child.hasType (ids::PATTERN))
            REQUIRE (child.getNumChildren() == 0);
}

TEST_CASE ("compiling from the editor writes the notes and says so", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    juce::String message;
    auto severity = StatusBar::Severity::info;
    editor.onMessage = [&] (const juce::String& text, StatusBar::Severity s)
    {
        message = text;
        severity = s;
    };

    editor.getSourceDocument().replaceAllContent (juce::String (workingSource()));
    editor.compileIntoProject();

    INFO ("message: " << message);
    REQUIRE (severity == StatusBar::Severity::success);
    REQUIRE (message.contains ("compiled"));

    auto notes = 0;

    for (const auto& child : document.getState())
        if (child.hasType (ids::PATTERN))
            notes += child.getNumChildren();

    REQUIRE (notes > 0);

    // And the source went with them, so the project is not a dead end.
    REQUIRE (ProjectEdits::scoreSource (document.getState()).toStdString() == workingSource());
}

TEST_CASE ("compiling a broken score changes nothing and says why", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    juce::String message;
    auto severity = StatusBar::Severity::info;
    editor.onMessage = [&] (const juce::String& text, StatusBar::Severity s)
    {
        message = text;
        severity = s;
    };

    editor.getSourceDocument().replaceAllContent ("song {\n  tempo 120\n  key C majorr\n}\n");
    const auto before = document.getState().createCopy();

    editor.compileIntoProject();

    INFO ("message: " << message);
    REQUIRE (severity == StatusBar::Severity::error);
    REQUIRE (message.contains ("error"));
    REQUIRE (message.contains ("nothing was written"));
    REQUIRE (document.getState().isEquivalentTo (before));
}

TEST_CASE ("a broken score is squiggled where it is broken", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    editor.getSourceDocument().replaceAllContent (juce::String (workingSource()));
    editor.checkNow();
    REQUIRE (editor.getDiagnostics().empty());

    const auto clean = coverageOf (render (editor), tokens::colour::danger);

    // One word changed, on one line.
    auto broken = workingSource();
    broken = juce::String (broken).replace ("key   C major", "key   C majorr").toStdString();

    editor.getSourceDocument().replaceAllContent (juce::String (broken));
    editor.checkNow();

    REQUIRE_FALSE (editor.getDiagnostics().empty());

    const auto marked = coverageOf (render (editor), tokens::colour::danger);

    // inkCoverage would be the wrong probe here and would fail open: the
    // editor's gutter fills most of the top-left, so "is there ink" is true of
    // an empty document. Asking for the DANGER colour specifically is a
    // question only a squiggle and its diagnostics row can answer.
    INFO ("danger coverage: clean " << clean << " -> marked " << marked);
    REQUIRE (marked > clean);
}

TEST_CASE ("a squiggle lands on the character it is about", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    editor.getSourceDocument().replaceAllContent ("song {\n  tempo 120\n  key C majorr\n}\n");
    editor.checkNow();
    editor.resized();

    REQUIRE_FALSE (editor.getDiagnostics().empty());

    const DiagnosticsOverlay overlay { editor };
    const auto& diagnostic = editor.getDiagnostics().front();
    const auto marked = overlay.boundsFor (diagnostic);

    const auto text = editor.getSourceDocument().getAllContent().toStdString();
    const juce::CodeDocument::Position start { editor.getSourceDocument(),
                                               ScoreEditorComponent::characterIndexForByte (
                                                   text, diagnostic.primary.begin) };

    const auto character = editor.getEditor().getCharacterBounds (start);

    INFO ("marked " << marked.toString() << " character " << character.toString());
    REQUIRE_FALSE (character.isEmpty());
    REQUIRE (marked.intersects (character));
}

TEST_CASE ("clicking a diagnostic puts the caret on it", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    // The error is far down, so moving to it is a real move.
    auto source = workingSource();
    source += "\nsection nowhere {\n  length 4 bars\n  harmony missing\n}\n";

    editor.getSourceDocument().replaceAllContent (juce::String (source));
    editor.checkNow();

    REQUIRE_FALSE (editor.getDiagnostics().empty());

    editor.showDiagnostic (0);

    const auto text = editor.getSourceDocument().getAllContent().toStdString();
    const auto expected = ScoreEditorComponent::characterIndexForByte (
        text, editor.getDiagnostics().front().primary.begin);

    REQUIRE (editor.getEditor().getCaretPos().getPosition() == expected);
}

TEST_CASE ("diagnostics are listed in source order", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    // A name error near the top and a syntax error near the bottom. The
    // compiler reports every parse error before any name error, so unsorted
    // this list jumps backwards - and it is a list whose whole job is to be
    // read down and clicked through.
    editor.getSourceDocument().replaceAllContent (
        "song {\n  tempo 120\n  meter 4/4\n  key C majorr\n}\n"
        "harmony h { I | vi }\n"
        "section verse {\n  length 4 bars\n  harmony h\n}\n"
        "arrangement {\n  verse\n  {\n}\n");

    editor.checkNow();

    REQUIRE (editor.getDiagnostics().size() > 1);

    for (std::size_t i = 1; i < editor.getDiagnostics().size(); ++i)
        REQUIRE (editor.getDiagnostics()[i - 1].primary.begin
                 <= editor.getDiagnostics()[i].primary.begin);
}

TEST_CASE ("a fresh project takes the score's grid", "[score][editor][bake]")
{
    // The difference between Compile working and Compile refusing on a new
    // project: nothing in an empty document has a meaning the grid could
    // change, and every interesting score needs more than four steps a beat.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    REQUIRE ((int) document.getState()[ids::stepsPerBeat] == 4);

    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    juce::String message;
    editor.onMessage = [&] (const juce::String& t, StatusBar::Severity) { message = t; };

    editor.getSourceDocument().replaceAllContent (juce::String (finerGridSource()));
    editor.compileIntoProject();

    INFO ("message: " << message);
    REQUIRE ((int) document.getState()[ids::stepsPerBeat] == 12);

    auto notes = 0;

    for (const auto& child : document.getState())
        if (child.hasType (ids::PATTERN))
            notes += child.getNumChildren();

    REQUIRE (notes > 0);
}

TEST_CASE ("a project with music in it keeps its own grid", "[score][editor][bake]")
{
    // The other half of the same rule. Once there are notes, stepsPerBeat owns
    // how long a step is, and adopting the score's would change how fast
    // everything already there plays.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    auto project = dew::testing::fixtureProject();
    const auto before = (int) project[ids::stepsPerBeat];

    const auto result = lang::compile (finerGridSource(), "t.score");
    REQUIRE (result.ok());
    REQUIRE (result.score->stepsPerBeat == 12); // or the test proves nothing

    const auto report = ScoreBake::into (project, *result.score, nullptr);

    REQUIRE_FALSE (report.warnings.isEmpty());
    REQUIRE (report.notesWritten == 0);
    REQUIRE ((int) project[ids::stepsPerBeat] == before);
}

TEST_CASE ("a project with no score offers one that works", "[score][editor]")
{
    // A blank rectangle is indistinguishable from a feature that is not there.
    // The starter has to compile, and it has to make a sound - the first thing
    // anybody presses Compile on should not be silence.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto starter = ScoreEditorComponent::starterScore().toStdString();

    const auto result = lang::compile (starter, "starter.score");
    INFO (result.report (starter, "starter.score"));
    REQUIRE (result.ok());
    REQUIRE (result.score->noteCount() > 0);

    BakeReport report;
    const auto project = ScoreBake::toNewProject (*result.score, report);

    juce::AudioBuffer<float> rendered;
    const auto rendering = OfflineRenderer::renderToBuffer (project, rendered);

    REQUIRE (rendering.ok());

    // Audible, and NOT clipping: the first sound dew makes from a score should
    // not be a distorted one. The starter's velocities were cut once already
    // for exactly this - it peaked at 1.06.
    INFO ("peak " << rendering.peak << " rms " << rendering.rms);
    REQUIRE (rendering.peak > 0.05f);
    REQUIRE (rendering.peak <= 1.0f);
    REQUIRE (rendering.rms > 0.01f);
}

TEST_CASE ("the starter is offered, not stored", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    // It is on screen...
    REQUIRE (editor.getSourceDocument().getAllContent() == ScoreEditorComponent::starterScore());

    // ...and the project is untouched, so merely opening the tab does not dirty
    // a project nobody has edited.
    editor.flushPendingCheck();
    REQUIRE (ProjectEdits::scoreSource (document.getState()).isEmpty());
    REQUIRE_FALSE (document.hasChangedSinceSaved());

    // One character typed makes it a document, and it is stored.
    editor.getSourceDocument().insertText (0, "// mine\n");
    editor.flushPendingCheck();

    REQUIRE (ProjectEdits::scoreSource (document.getState()).startsWith ("// mine"));
}

TEST_CASE ("a project that has a score shows that, not the starter", "[score][editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ProjectEdits::setScoreSource (document.getState(), juce::String (workingSource()), "t.score",
                                  nullptr);

    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    REQUIRE (editor.getSourceDocument().getAllContent().toStdString() == workingSource());
}

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

TEST_CASE ("the score's text size steps, and stops at both ends", "[score][editor][type]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    const auto opensAt = ScoreEditorComponent::defaultFontStep();
    REQUIRE (editor.getFontStep() == opensAt);

    // The tab opens on the rung the ladder names for a document, and the
    // editor is actually drawing at it - a step nothing applied would pass a
    // round-trip test and change nothing on screen.
    CHECK (juce::exactlyEqual (editor.getEditor().getFont().getHeight(), tokens::type::codeBody));

    editor.setFontStep (opensAt + 1);
    CHECK (editor.getFontStep() == opensAt + 1);
    CHECK (editor.getEditor().getFont().getHeight() > tokens::type::codeBody);

    // Clamped rather than wrapped, at both ends. dew_app stores this number and
    // cannot check it, so this is the only place that can.
    editor.setFontStep (100);
    CHECK (editor.getFontStep() == ScoreEditorComponent::numFontSteps() - 1);
    CHECK (juce::exactlyEqual (editor.getEditor().getFont().getHeight(), tokens::type::codeHuge));

    editor.setFontStep (-100);
    CHECK (editor.getFontStep() == 0);
    CHECK (juce::exactlyEqual (editor.getEditor().getFont().getHeight(), tokens::type::codeSmall));
}

TEST_CASE ("alt and the zoom keys size the score's text", "[score][editor][type]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 600);

    const auto opensAt = editor.getFontStep();

    // Reached as a KeyListener on the CodeEditorComponent, which is how the
    // completion popup already owns its keys - so this is the path a real
    // press takes, not a back door.
    CHECK (editor.keyPressed (juce::KeyPress ('=', juce::ModifierKeys::altModifier, 0),
                              &editor.getEditor()));
    CHECK (editor.getFontStep() == opensAt + 1);

    CHECK (editor.keyPressed (juce::KeyPress ('-', juce::ModifierKeys::altModifier, 0),
                              &editor.getEditor()));
    CHECK (editor.getFontStep() == opensAt);

    CHECK (editor.keyPressed (juce::KeyPress ('0', juce::ModifierKeys::altModifier, 0),
                              &editor.getEditor()));
    CHECK (editor.getFontStep() == ScoreEditorComponent::defaultFontStep());

    // THE thing that must not happen: a bare `=` or `0` is a character somebody
    // is typing into the document, not a command. The timeline map binds both
    // bare, which is exactly why these are on alt.
    CHECK_FALSE (editor.keyPressed (juce::KeyPress ('='), &editor.getEditor()));
    CHECK_FALSE (editor.keyPressed (juce::KeyPress ('0'), &editor.getEditor()));
    CHECK_FALSE (editor.keyPressed (juce::KeyPress ('1'), &editor.getEditor()));
    CHECK (editor.getFontStep() == ScoreEditorComponent::defaultFontStep());
}
