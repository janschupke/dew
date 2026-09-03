#include "ui/ScoreEditorComponent.h"

#include <algorithm>

#include "lang/SourceRange.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ScoreBake.h"
#include "ui/design/Tokens.h"

namespace dew
{

namespace
{

/** How many rows of diagnostics are shown before the list scrolls. */
constexpr int diagnosticRows = 4;

juce::Colour colourFor (lang::Severity severity)
{
    return severity == lang::Severity::error ? tokens::colour::danger
                                             : tokens::colour::warning;
}

/** A wavy line under a span, drawn in the language of the spacing scale so it
    is the same weight as everything else that marks something.
*/
void paintSquiggle (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour colour)
{
    if (area.getWidth() <= 0)
        return;

    const auto amplitude = (float) tokens::space::xxs;
    const auto period = (float) tokens::space::xs;
    const auto baseline = (float) area.getBottom() - amplitude;

    juce::Path wave;
    wave.startNewSubPath ((float) area.getX(), baseline);

    auto up = true;

    for (auto x = (float) area.getX(); x < (float) area.getRight(); x += period)
    {
        wave.lineTo (juce::jmin (x + period, (float) area.getRight()),
                     up ? baseline - amplitude : baseline);
        up = ! up;
    }

    g.setColour (colour);
    g.strokePath (wave, juce::PathStrokeType (tokens::stroke::hairline));
}

} // namespace

// --- the overlay -------------------------------------------------------------

DiagnosticsOverlay::DiagnosticsOverlay (ScoreEditorComponent& editorOwner)
    : owner (editorOwner)
{
    // The whole point: it decorates, it never receives. A click goes to the
    // editor underneath, caret and all.
    setInterceptsMouseClicks (false, false);
    startTimerHz (tokens::motion::uiRefreshHz);
}

void DiagnosticsOverlay::timerCallback()
{
    // CodeEditorComponent announces nothing when it scrolls, so the top line is
    // the only signal there is. Comparing it means a still editor repaints
    // never, rather than thirty times a second.
    const auto topLine = owner.getEditor().getFirstLineOnScreen();

    if (topLine != lastTopLine)
    {
        lastTopLine = topLine;
        repaint();
    }
}

juce::Rectangle<int> DiagnosticsOverlay::boundsFor (const lang::Diagnostic& diagnostic) const
{
    auto& editor = owner.getEditor();
    auto& document = owner.getSourceDocument();
    const auto text = document.getAllContent().toStdString();

    const auto first = ScoreEditorComponent::characterIndexForByte (text, diagnostic.primary.begin);
    const auto last = ScoreEditorComponent::characterIndexForByte (text, diagnostic.primary.end);

    const juce::CodeDocument::Position start { document, first };
    const juce::CodeDocument::Position end { document, juce::jmax (first + 1, last) };

    const auto startBounds = editor.getCharacterBounds (start);

    // A range spanning lines is marked on its FIRST line only. A squiggle
    // wrapping around three lines says less than one under the token that is
    // wrong, and the list below the editor says the rest.
    const auto endBounds = end.getLineNumber() == start.getLineNumber()
                               ? editor.getCharacterBounds (end)
                               : startBounds.withX (startBounds.getRight());

    return startBounds.getUnion (endBounds.translated (-endBounds.getWidth(), 0))
                      .withRight (juce::jmax (startBounds.getRight(), endBounds.getX()));
}

void DiagnosticsOverlay::paint (juce::Graphics& g)
{
    for (const auto& diagnostic : owner.getDiagnostics())
    {
        const auto area = boundsFor (diagnostic);

        if (area.isEmpty() || ! getLocalBounds().intersects (area))
            continue;

        paintSquiggle (g, area, colourFor (diagnostic.severity));
    }
}

// --- the editor --------------------------------------------------------------

ScoreEditorComponent::ScoreEditorComponent (ProjectDocument& projectDocument)
    : document (projectDocument)
{
    setComponentID ("scoreEditor");

    heading.setText ("Score", juce::dontSendNotification);
    heading.setFont (tokens::type::font (tokens::type::title, true));
    heading.setColour (juce::Label::textColourId, tokens::colour::textPrimary);
    addAndMakeVisible (heading);

    compileButton.setComponentID ("scoreCompile");
    compileButton.onClick = [this] { compileIntoProject(); };
    addAndMakeVisible (compileButton);

    editor.setComponentID ("scoreText");
    editor.setFont (tokens::type::monospaced (tokens::type::body));
    editor.setColourScheme (ScoreTokeniser::scheme());
    editor.setColour (juce::CodeEditorComponent::backgroundColourId, tokens::colour::wellDeep);
    editor.setColour (juce::CodeEditorComponent::lineNumberBackgroundId, tokens::colour::well);
    editor.setColour (juce::CodeEditorComponent::lineNumberTextId, tokens::colour::textDisabled);
    editor.setColour (juce::CodeEditorComponent::highlightColourId,
                      tokens::colour::accent.withAlpha (tokens::emphasis::wash));
    editor.setColour (juce::CodeEditorComponent::defaultTextColourId, tokens::colour::textPrimary);
    addAndMakeVisible (editor);

    // Over the editor, and added after it so it paints on top.
    addAndMakeVisible (overlay);

    list.setComponentID ("scoreDiagnosticsList");
    list.setRowHeight (tokens::size::controlHeightSm);
    list.setColour (juce::ListBox::backgroundColourId, tokens::colour::well);
    addAndMakeVisible (list);

    source.addListener (this);

    refresh();
}

ScoreEditorComponent::~ScoreEditorComponent()
{
    source.removeListener (this);
}

int ScoreEditorComponent::characterIndexForByte (const std::string& utf8,
                                                 std::uint32_t byteOffset)
{
    const auto limit = juce::jmin ((std::size_t) byteOffset, utf8.size());
    auto characters = 0;

    for (std::size_t i = 0; i < limit; ++i)
        if (((unsigned char) utf8[i] & 0xC0u) != 0x80u)   // not a continuation byte
            ++characters;

    return characters;
}

void ScoreEditorComponent::paint (juce::Graphics& g)
{
    g.fillAll (tokens::colour::background);
}

void ScoreEditorComponent::resized()
{
    auto area = getLocalBounds();

    auto strip = area.removeFromTop (tokens::size::stripToolbar).reduced (tokens::space::sm,
                                                                         tokens::space::xs);
    compileButton.setBounds (strip.removeFromRight (tokens::size::gutterLabel));
    strip.removeFromRight (tokens::space::md);
    heading.setBounds (strip);

    if (! diagnostics.empty())
    {
        list.setVisible (true);
        list.setBounds (area.removeFromBottom (tokens::size::controlHeightSm * diagnosticRows));
    }
    else
    {
        list.setVisible (false);
    }

    editor.setBounds (area);
    overlay.setBounds (area);
}

void ScoreEditorComponent::refresh()
{
    const auto stored = ProjectEdits::scoreSource (document.getState());

    // Only when the project changed underneath. Reloading whenever anything
    // refreshed would take the text out from under somebody typing.
    if (stored == mirrored)
        return;

    mirrored = stored;
    source.replaceAllContent (stored);
    checkNow();
}

void ScoreEditorComponent::codeDocumentTextInserted (const juce::String&, int)
{
    startTimer (tokens::motion::typingPauseMs);
}

void ScoreEditorComponent::codeDocumentTextDeleted (int, int)
{
    startTimer (tokens::motion::typingPauseMs);
}

void ScoreEditorComponent::timerCallback()
{
    stopTimer();
    checkNow();
    storeSource();
}

void ScoreEditorComponent::flushPendingCheck()
{
    if (isTimerRunning())
        timerCallback();
}

void ScoreEditorComponent::checkNow()
{
    const auto text = source.getAllContent().toStdString();

    ++checkCount;
    checkedText = text;

    // Compile rather than parse-only: generation is microseconds for a piece
    // this size, and it is where the errors worth seeing live - an overfull
    // progression, a grid that cannot be represented. Nothing is written.
    const auto result = lang::compile (text, ProjectEdits::scoreSourceName (document.getState())
                                                 .toStdString());

    diagnostics = result.diagnostics;

    // In SOURCE order, not in the order the phases produced them. The compiler
    // reports every parse error before any name error, so an unsorted list
    // jumps from line 67 to line 11 and back - and this is a list whose whole
    // job is to be read down and clicked through.
    std::stable_sort (diagnostics.begin(), diagnostics.end(),
                      [] (const lang::Diagnostic& a, const lang::Diagnostic& b)
                      { return a.primary.begin < b.primary.begin; });

    resized();
    list.updateContent();
    list.repaint();
    overlay.repaint();
}

void ScoreEditorComponent::storeSource()
{
    const auto text = source.getAllContent();

    if (text == mirrored)
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Edit score");

    const auto name = ProjectEdits::scoreSourceName (document.getState());
    ProjectEdits::setScoreSource (document.getState(), text,
                                  name.isEmpty() ? "score" : name, &undo);

    mirrored = text;
}

void ScoreEditorComponent::compileIntoProject()
{
    checkNow();

    const auto text = source.getAllContent().toStdString();
    const auto name = ProjectEdits::scoreSourceName (document.getState());
    const auto result = lang::compile (text, name.isEmpty() ? "score" : name.toStdString());

    if (! result.ok())
    {
        const auto errors = result.errorCount();
        say ("Score has " + juce::String (errors) + (errors == 1 ? " error" : " errors")
                 + " - nothing was written",
             StatusBar::Severity::error);
        return;
    }

    // The bake opens the transaction; storing the text JOINS it, so compiling
    // is one undo step covering both the notes and the source they came from.
    auto& undo = document.getUndoManager();
    const auto report = ScoreBake::into (document.getState(), *result.score, &undo);

    ProjectEdits::setScoreSource (document.getState(), source.getAllContent(),
                                  name.isEmpty() ? "score" : name, &undo);
    mirrored = source.getAllContent();

    if (! report.warnings.isEmpty())
    {
        say (report.warnings[0], StatusBar::Severity::warning);
        return;
    }

    juce::String message = "Score compiled: " + juce::String (report.patternsWritten)
                         + " pattern" + (report.patternsWritten == 1 ? "" : "s") + ", "
                         + juce::String (report.clipsWritten) + " clip"
                         + (report.clipsWritten == 1 ? "" : "s") + ", "
                         + juce::String (report.notesWritten) + " note"
                         + (report.notesWritten == 1 ? "" : "s");

    if (report.patternsKept > 0)
        message << " - " << report.patternsKept << " pattern"
                << (report.patternsKept == 1 ? " was" : "s were")
                << " edited by hand and left alone";

    say (message, StatusBar::Severity::success);
}

void ScoreEditorComponent::showDiagnostic (int index)
{
    if (index < 0 || index >= (int) diagnostics.size())
        return;

    const auto text = source.getAllContent().toStdString();
    const juce::CodeDocument::Position position {
        source, characterIndexForByte (text, diagnostics[(std::size_t) index].primary.begin)
    };

    editor.moveCaretTo (position, false);
    editor.scrollToKeepCaretOnScreen();
    editor.grabKeyboardFocus();
}

void ScoreEditorComponent::say (const juce::String& message, StatusBar::Severity severity)
{
    if (onMessage != nullptr)
        onMessage (message, severity);
}

// --- the diagnostics list ----------------------------------------------------

int ScoreEditorComponent::getNumRows()
{
    return (int) diagnostics.size();
}

void ScoreEditorComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                                             bool selected)
{
    if (row < 0 || row >= (int) diagnostics.size())
        return;

    const auto& diagnostic = diagnostics[(std::size_t) row];

    if (selected)
        g.fillAll (tokens::colour::accent.withAlpha (tokens::emphasis::tint));

    const lang::LineIndex index { checkedText };
    const auto line = index.lineAt (diagnostic.primary.begin);
    const auto column = index.columnAt (diagnostic.primary.begin);

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (tokens::space::sm, 0);

    g.setFont (tokens::type::monospaced (tokens::type::small));
    g.setColour (colourFor (diagnostic.severity));
    g.drawText (juce::String (line) + ":" + juce::String (column),
                area.removeFromLeft (tokens::size::gutterLabel), juce::Justification::centredLeft);

    g.setColour (tokens::colour::textSecondary);
    g.drawText (juce::String (diagnostic.code),
                area.removeFromLeft (tokens::size::letterToggle * 2),
                juce::Justification::centredLeft);

    g.setFont (tokens::type::font (tokens::type::small));
    g.setColour (tokens::colour::textPrimary);
    g.drawText (juce::String (diagnostic.message), area, juce::Justification::centredLeft, true);
}

void ScoreEditorComponent::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    showDiagnostic (row);
}

} // namespace dew
