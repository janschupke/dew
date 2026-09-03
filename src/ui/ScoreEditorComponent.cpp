#include "ui/ScoreEditorComponent.h"

#include <algorithm>
#include <cmath>

#include "lang/Completion.h"
#include "lang/SourceRange.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ScoreBake.h"
#include "ui/design/Gestures.h"
#include "ui/Hotkeys.h"
#include "ui/design/Tokens.h"

namespace dew
{

namespace
{

/** The sizes the score's text steps through, smallest first.

    Here rather than in Tokens.h as an array, because a table inside the token
    file would satisfy the unused-token gate for all four rungs without the
    application ever quoting one - the gate reads every file EXCEPT Tokens.h
    for exactly that reason.
*/
constexpr float fontSteps[] = { tokens::type::codeSmall, tokens::type::codeBody,
                                tokens::type::codeLarge, tokens::type::codeHuge };

constexpr int numSteps = (int) (sizeof (fontSteps) / sizeof (fontSteps[0]));

/** codeBody: the rung the tab opens at, named once rather than spelled as 1. */
constexpr int bodyStep = 1;

static_assert (fontSteps[0] < fontSteps[1] && fontSteps[1] < fontSteps[2]
                   && fontSteps[2] < fontSteps[3],
               "the steps have to increase, or bigger and smaller swap over");

// exactlyEqual, not ==: the ci preset builds -Wfloat-equal, and these two are
// the same constant reached two ways rather than two computed numbers.
static_assert (juce::exactlyEqual (fontSteps[bodyStep], tokens::type::codeBody),
               "the default step has to be the rung it is named after");

/** What the Score tab shows when a project has no score in it.

    A blank rectangle is indistinguishable from a feature that is not there, and
    a language nobody can see the shape of is a language nobody writes. This one
    compiles, and a test renders it to check it is audible and does not clip -
    the first thing anybody presses Compile on should make a sound.

    It is NOT written into the project until it is edited or compiled, so
    opening the tab does not dirty a project nobody has touched.
*/
const char* const starterScoreText =
    R"SCORE(// A score describes a whole song as text: its key, its chords, its sections,
// and a rule per instrument for what to play over them. Press Compile, or
// Command-R, and it becomes patterns and clips you can edit like any others.
//
// This one plays. Change a chord, change `variance`, compile again.

song {
  title "Untitled"
  tempo 110 bpm
  meter 4/4
  key   A minor
  seed  0x5C0DED
}

channel pad {
  mixer    1
  range    C3..C5
  velocity 54 +- 5
}

channel bass {
  mixer    2
  range    E1..E3
  velocity 74 +- 4
}

channel lead {
  mixer    3
  range    A3..A5
  velocity 66 +- 8
}

voicing warm {
  size     4 voices
  spread   drop2
  register C3..C5
  motion   smooth
}

rhythm held  { 1/1 }
rhythm pulse { 1/4 1/4 1/2 }
rhythm line  { 1/8 1/8 1/4 }

harmony loop {
  i | bVI | bIII | bVII
}

section verse {
  length 4 bars
  harmony loop

  part pad {
    chords with warm
    rhythm held
  }

  part bass {
    line root
    rhythm pulse
  }

  part lead {
    melody {
      rhythm   line
      contour  arch
      strong   chord-tones
      variance 0.3
      mute     1 of 4
    }
  }
}

arrangement {
  verse
  verse
}
)SCORE";

/** How many rows of diagnostics are shown before the list scrolls. */
constexpr int diagnosticRows = 4;

juce::Colour colourFor (lang::Severity severity)
{
    return severity == lang::Severity::error ? tokens::colour::danger : tokens::colour::warning;
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

    // The hint is the discoverability: nothing else on screen says the popup
    // exists, and a completion nobody knows how to ask for is one nobody uses.
    // CharPointer_UTF8, not a bare literal: juce::String's const char*
    // constructor decodes ASCII, so the control glyph came out as two mojibake
    // characters on screen.
    heading.setText (juce::String (juce::CharPointer_UTF8 ("Score   \xe2\x8c\x83Space completes")),
                     juce::dontSendNotification);
    heading.setFont (tokens::type::font (tokens::type::title, true));
    heading.setColour (juce::Label::textColourId, tokens::colour::textPrimary);
    addAndMakeVisible (heading);

    compileButton.setComponentID ("scoreCompile");
    compileButton.setTooltip ("Turn this score into patterns, notes and clips (cmd-R)");
    compileButton.onClick = [this] { compileIntoProject(); };
    addAndMakeVisible (compileButton);

    editor.setComponentID ("scoreText");
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

    completions.setVisible (false);
    completions.onAccept = [this] { acceptCompletion(); };
    addChildComponent (completions);

    source.addListener (this);

    // BEFORE the editor, so the popup can own Up, Down, Return and Escape while
    // it is open. A key listener is the whole of it - subclassing
    // CodeEditorComponent to intercept them would mean owning its layout too.
    editor.addKeyListener (this);

    // After every colour and before the first paint, so the editor is never
    // laid out at a size it does not keep.
    setFontStep (bodyStep);

    refresh();
}

void ScoreTextEditor::mouseWheelMove (const juce::MouseEvent& event,
                                      const juce::MouseWheelDetails& wheel)
{
    const auto delta = gesture::deltaOf (wheel);

    // A sideways gesture is the base class's business; see the class comment.
    if (std::abs (delta.y) < std::abs (delta.x))
    {
        juce::CodeEditorComponent::mouseWheelMove (event, wheel);
        return;
    }

    lineRemainder += delta.y * gesture::wheelPixelsPerNotch
                     / (double) juce::jmax (1, getLineHeight());

    const auto lines = (int) std::trunc (lineRemainder);

    if (lines == 0)
        return;

    lineRemainder -= (double) lines;
    scrollBy (-lines);
}

int ScoreEditorComponent::numFontSteps() noexcept
{
    return numSteps;
}
int ScoreEditorComponent::defaultFontStep() noexcept
{
    return bodyStep;
}

void ScoreEditorComponent::setFontStep (int step)
{
    fontStep = juce::jlimit (0, numSteps - 1, step);
    applyFontStep();
}

void ScoreEditorComponent::applyFontStep()
{
    editor.setFont (tokens::type::monospaced (fontSteps[fontStep]));

    // The overlay draws its squiggles from the editor's own metrics, so it is
    // stale the moment those change.
    overlay.repaint();
    resized();
}

ScoreEditorComponent::~ScoreEditorComponent()
{
    editor.removeKeyListener (this);
    source.removeListener (this);
}

int ScoreEditorComponent::characterIndexForByte (const std::string& utf8, std::uint32_t byteOffset)
{
    const auto limit = juce::jmin ((std::size_t) byteOffset, utf8.size());
    auto characters = 0;

    for (std::size_t i = 0; i < limit; ++i)
        if (((unsigned char) utf8[i] & 0xC0u) != 0x80u) // not a continuation byte
            ++characters;

    return characters;
}

int ScoreEditorComponent::byteIndexForCharacter (const std::string& utf8, int characterIndex)
{
    auto characters = 0;

    for (std::size_t i = 0; i < utf8.size(); ++i)
    {
        if (((unsigned char) utf8[i] & 0xC0u) != 0x80u) // not a continuation byte
        {
            if (characters == characterIndex)
                return (int) i;

            ++characters;
        }
    }

    return (int) utf8.size();
}

// --- completion --------------------------------------------------------------

bool ScoreEditorComponent::isCompletionVisible() const
{
    return completions.isVisible();
}

void ScoreEditorComponent::hideCompletions()
{
    completions.setVisible (false);
}

void ScoreEditorComponent::showCompletions()
{
    const auto text = source.getAllContent().toStdString();
    const auto offset = byteIndexForCharacter (text, editor.getCaretPos().getPosition());

    const auto result = lang::completionsAt (text, (std::uint32_t) offset);

    if (result.items.empty())
    {
        hideCompletions();
        return;
    }

    completions.setItems (result.items);

    // Under the caret, and shoved back on screen rather than off the bottom or
    // the right - a popup you cannot see is worse than none.
    const auto caret = editor.getCharacterBounds (editor.getCaretPos())
                           .translated (editor.getX(), editor.getY());

    const auto width = juce::jmin (getWidth() - tokens::space::xl, tokens::size::gutterChannel);
    const auto height = completions.preferredHeight();

    auto x = juce::jlimit (0, juce::jmax (0, getWidth() - width), caret.getX());
    auto y = caret.getBottom() + tokens::space::xxs;

    if (y + height > getHeight())
        y = juce::jmax (0, caret.getY() - height - tokens::space::xxs);

    completions.setBounds (x, y, width, height);
    completions.setVisible (true);
    completions.toFront (false);
}

void ScoreEditorComponent::acceptCompletion()
{
    const auto* selected = completions.getSelected();

    if (selected == nullptr)
    {
        hideCompletions();
        return;
    }

    const auto text = source.getAllContent().toStdString();
    const auto offset = byteIndexForCharacter (text, editor.getCaretPos().getPosition());
    const auto result = lang::completionsAt (text, (std::uint32_t) offset);

    // The partial word is REPLACED, not appended to, or accepting `channel`
    // after `cha` spells `chachannel`.
    const juce::CodeDocument::Position from {
        source, result.replacing.isEmpty() ? editor.getCaretPos().getPosition()
                                           : characterIndexForByte (text, result.replacing.begin)
    };

    const juce::CodeDocument::Position to { source, editor.getCaretPos().getPosition() };

    hideCompletions();

    source.replaceSection (from.getPosition(), to.getPosition(), juce::String (selected->text));

    editor.moveCaretTo (
        juce::CodeDocument::Position (source, from.getPosition() + (int) selected->text.size()),
        false);
    editor.grabKeyboardFocus();
}

bool ScoreEditorComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    // Control-Space asks, whether or not the popup is already open - asking
    // again after typing three more letters is the ordinary way to use it.
    if (key.getKeyCode() == juce::KeyPress::spaceKey && key.getModifiers().isCtrlDown())
    {
        showCompletions();
        return true;
    }

    // The size trio, read from the one key table rather than spelled here. Alt
    // rather than command, so a bare `=` still arrives in the document as an
    // `=` - and so the same three keys mean "the other size" in the timelines
    // too, where the other size is a row height.
    switch (hotkeys::viewCommandFor (key))
    {
        case hotkeys::ViewCommand::sizeBigger: setFontStep (fontStep + 1); return true;
        case hotkeys::ViewCommand::sizeSmaller: setFontStep (fontStep - 1); return true;
        case hotkeys::ViewCommand::sizeDefault: setFontStep (bodyStep); return true;

        // Everything else the timeline map knows is a key this editor must let
        // through: `1` is a digit somebody is typing, not the select tool.
        case hotkeys::ViewCommand::none:
        case hotkeys::ViewCommand::zoomIn:
        case hotkeys::ViewCommand::zoomOut:
        case hotkeys::ViewCommand::zoomToFit:
        case hotkeys::ViewCommand::selectTool:
        case hotkeys::ViewCommand::paintTool:
        case hotkeys::ViewCommand::eraseTool:
        case hotkeys::ViewCommand::clearSelection:
        case hotkeys::ViewCommand::deleteSelection:
        case hotkeys::ViewCommand::selectAll: break;
    }

    if (! isCompletionVisible())
        return false;

    if (key == juce::KeyPress (juce::KeyPress::escapeKey))
    {
        hideCompletions();
        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::upKey))
    {
        completions.moveSelection (-1);
        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::downKey))
    {
        completions.moveSelection (1);
        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::returnKey)
        || key == juce::KeyPress (juce::KeyPress::tabKey))
    {
        acceptCompletion();
        return true;
    }

    // Anything else - a letter, a space, an arrow sideways - goes to the editor
    // and closes the popup, because what it was offering is no longer what the
    // caret is on. Reopening is one keystroke.
    hideCompletions();
    return false;
}

void ScoreEditorComponent::paint (juce::Graphics& g)
{
    g.fillAll (tokens::colour::background);
}

void ScoreEditorComponent::resized()
{
    auto area = getLocalBounds();

    auto strip = area.removeFromTop (tokens::size::stripToolbar)
                     .reduced (tokens::space::sm, tokens::space::xs);
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

juce::String ScoreEditorComponent::starterScore()
{
    return starterScoreText;
}

void ScoreEditorComponent::refresh()
{
    const auto stored = ProjectEdits::scoreSource (document.getState());

    if (stored.isEmpty())
    {
        // Nothing written for this project yet. Show something readable and
        // compilable rather than an empty rectangle, which is indistinguishable
        // from a feature that is not there.
        //
        // Only over an empty editor or over the starter itself: once somebody
        // has typed, this must not reach in and replace it.
        const auto showing = source.getAllContent();

        if (showing.isEmpty() || showing == starterScore())
        {
            mirrored = {};
            source.replaceAllContent (starterScore());
            checkNow();
        }

        return;
    }

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
    const auto result = lang::compile (
        text, ProjectEdits::scoreSourceName (document.getState()).toStdString());

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

    // The starter, untouched, is an offer rather than a document. Storing it
    // would mean opening the tab dirtied a project nobody had edited - and then
    // every new project would carry a score describing music it does not have.
    if (mirrored.isEmpty() && text == starterScore())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Edit score");

    const auto name = ProjectEdits::scoreSourceName (document.getState());
    ProjectEdits::setScoreSource (document.getState(), text, name.isEmpty() ? "score" : name,
                                  &undo);

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

    juce::String message = "Score compiled: " + juce::String (report.patternsWritten) + " pattern"
                           + (report.patternsWritten == 1 ? "" : "s") + ", "
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
