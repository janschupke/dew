#include "ui/ScoreEditorComponent.h"

#include <algorithm>
#include <cmath>

#include "i18n/Strings.h"
#include "lang/Completion.h"
#include "lang/SourceRange.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ScoreBake.h"
#include "ui/design/Gestures.h"
#include "ui/Hotkeys.h"
#include "ui/ScoreEditorLayout.h"
#include "ui/ScoreLocale.h"
#include "ui/design/Tokens.h"

namespace dew
{

// --- the editor --------------------------------------------------------------

ScoreEditorComponent::ScoreEditorComponent (ProjectDocument& projectDocument)
    : document (projectDocument)
{
    setComponentID ("scoreEditor");

    // Without this the score tab contributes nothing to the group ring: its
    // compile button, its heading and its diagnostics list would belong to no
    // group, and ctrl-tab in front of it would skip straight past the editor.
    setTitle (tr (StringId::score_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    // The hint is the discoverability: nothing else on screen says the popup
    // exists, and a completion nobody knows how to ask for is one nobody uses.
    //
    // This was the last English sentence written into a source file, and it
    // survived the first extraction pass by wearing a juce::String around it:
    // the gate asked what setText's argument STARTED with, and the answer was
    // "juce::String (", not a quote. The gate now looks through that wrapper,
    // which is what found this.
    heading.setText (tr (StringId::score_heading), juce::dontSendNotification);
    heading.setFont (tokens::type::font (tokens::type::title, true));
    addAndMakeVisible (heading);

    compileButton.setComponentID ("scoreCompile");
    compileButton.setTooltip (tr (StringId::score_compile_help));
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
        // through: `1` is a digit somebody is typing, not the select tool, and
        // the arrows are how a caret moves through a document. The three
        // canvases put a cursor on those because they paint their contents and
        // have nothing else for a keyboard to land on; a text editor already
        // is a keyboard interface and must not have them taken away.
        case hotkeys::ViewCommand::cursorLeft:
        case hotkeys::ViewCommand::cursorRight:
        case hotkeys::ViewCommand::cursorUp:
        case hotkeys::ViewCommand::cursorDown:
        case hotkeys::ViewCommand::cursorActivate:
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
    // The introduction and the score, not one literal: the first is four
    // sentences a person reads and the second is a program dew compiles.
    auto starter = tr (StringId::score_starter_intro);
    starter += starterScoreBody;
    return starter;
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
    const auto result = lang::compile (text, scoreLocale());

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
    const auto result = lang::compile (text, scoreLocale());

    if (! result.ok())
    {
        const auto errors = result.errorCount();
        say (tr (StringId::score_errors, Args {}.count (errors)), StatusBar::Severity::error);
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

    // TWO whole messages rather than one with a clause appended. The clause
    // used to be its own key beginning " - ", which made the joiner and the
    // order of the two halves facts about English that no translator could
    // move - the shape i18n.md means by "never assemble a sentence with +".
    const auto kept = report.patternsKept > 0;

    auto arguments = Args {}
                         .with ("patterns", report.patternsWritten)
                         .with ("clips", report.clipsWritten)
                         .with ("notes", report.notesWritten);

    if (kept)
        arguments.with ("kept", report.patternsKept);

    say (tr (kept ? StringId::score_compiledWithKept : StringId::score_compiled, arguments),
         StatusBar::Severity::success);
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
    g.drawText (tr (StringId::score_position, Args {}.with ("line", line).with ("column", column)),
                area.removeFromLeft (tokens::size::gutterLabel), juce::Justification::centredLeft);

    g.setColour (tokens::colour::textSecondary);
    g.drawText (juce::String (diagnostic.code),
                area.removeFromLeft (tokens::size::letterToggle * 2),
                juce::Justification::centredLeft);

    g.setFont (tokens::type::font (tokens::type::small));
    g.setColour (tokens::colour::textPrimary);
    // CharPointer_UTF8, not the const char* constructor. A Diagnostic's message
    // is now catalogue text, so the first locale with an accent in it would have
    // rendered here as mojibake - the trap cpp-style.md names, reached through a
    // door the concatenation gate does not watch, because there is no
    // concatenation.
    g.drawText (juce::String (juce::CharPointer_UTF8 (diagnostic.message.c_str())), area,
                juce::Justification::centredLeft, true);
}

void ScoreEditorComponent::listBoxItemClicked (int row, const juce::MouseEvent& event)
{
    // See ScoreCompletionList: a right-click on a diagnostic row was jumping
    // the caret to it.
    if (event.mods.isPopupMenu())
        return;

    showDiagnostic (row);
}

} // namespace dew
