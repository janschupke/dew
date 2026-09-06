#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <string>
#include <vector>

#include "lang/SourceRange.h"
#include "lang/Compile.h"
#include "app/ProjectDocument.h"
#include "ui/ScoreCompletionList.h"
#include "ui/ScoreTokeniser.h"
#include "ui/StatusBar.h"
#include "ui/primitives/DewButtons.h"

namespace dew
{

class ScoreEditorComponent;

/** The squiggles, painted over the editor rather than by it.

    juce::CodeEditorComponent has no hook for decorating a range, and
    subclassing its painter would mean owning its layout. A mouse-transparent
    sibling sharing its bounds needs neither: it asks the editor where a
    character IS - getCharacterBounds, which works with no peer and no message
    loop - and draws under it.

    It has to repaint on scroll, and CodeEditorComponent broadcasts nothing when
    it scrolls, so this watches the top line on a timer. That is the same idiom
    the meters and the scope already use, and it is why the rate is a token.
*/
class DiagnosticsOverlay : public juce::Component, private juce::Timer
{
public:
    explicit DiagnosticsOverlay (ScoreEditorComponent&);

    void paint (juce::Graphics&) override;

    /** The rectangle a diagnostic is drawn under, for a test to compare with
        the editor's own idea of where that character is.
    */
    juce::Rectangle<int> boundsFor (const lang::Diagnostic&) const;

private:
    void timerCallback() override;

    ScoreEditorComponent& owner;
    int lastTopLine = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiagnosticsOverlay)
};

// -----------------------------------------------------------------------------

/** The score language, edited in the application it compiles for.

    Two things happen here and they are deliberately not the same thing:

    - **Checking** runs on a debounce as you type. It lexes, parses and
      resolves, and it touches nothing: no notes, no undo entry, no dirty flag.
      That is what makes live diagnostics safe.
    - **Compiling** happens only when asked, from the button or Command-R. It
      writes patterns, notes and clips into the project in one undo transaction.

    A debounced auto-compile would put an undo step on every pause in typing and
    would replace hand edits without being asked, which is the one thing the
    recompile policy exists to avoid.

    The source text itself is written back to the project on the same debounce,
    through ProjectEdits, so a score survives a save without anybody pressing
    anything - one transaction per typing run, not one per keystroke.
*/
/** The score's text, scrolling at the speed everything else scrolls at.

    juce::CodeEditorComponent forwards the wheel to its own scrollbars, which
    move in LINES at a rate JUCE picks - a sixth speed in an application that
    now has one. This subclass is that one change and nothing else: the layout,
    the painting, the caret and the keyboard are all still the base class's.

    Horizontal scrolling is left to it. A score has short lines and the axis is
    rarely used, and matching one gesture is not worth reimplementing the other.
*/
class ScoreTextEditor : public juce::CodeEditorComponent
{
public:
    using juce::CodeEditorComponent::CodeEditorComponent;

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    /** Whole lines are what scrollBy takes, and a trackpad sends fractions of
        one. Rounding each event on its own means a slow drag scrolls nothing at
        all, so the remainder is carried to the next.
    */
    double lineRemainder = 0.0;
};

class ScoreEditorComponent : public juce::Component,
                             private juce::CodeDocument::Listener,
                             private juce::Timer,
                             private juce::ListBoxModel,
                             private juce::KeyListener
{
public:
    explicit ScoreEditorComponent (ProjectDocument&);
    ~ScoreEditorComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Re-reads the score from the project, when the document was replaced
        underneath - File > Open, File > New, a demo.

        Does nothing while the text in the tree is the text this editor last
        put there, so it cannot interrupt somebody typing.
    */
    void refresh();

    /** Compiles and writes the result into the project. Always explicit. */
    void compileIntoProject();

    /** Lex, parse, resolve. Called on the debounce; safe to call by hand. */
    void checkNow();

    const std::vector<lang::Diagnostic>& getDiagnostics() const
    {
        return diagnostics;
    }

    /** How many times the text has been checked. A test types ten characters,
        advances the clock once, and asserts this went up by one.
    */
    int getCheckCount() const
    {
        return checkCount;
    }

    /** True while an edit is waiting for typing to stop. */
    bool isCheckPending() const
    {
        return isTimerRunning();
    }

    /** Fires the pending check now, as if typing had paused.

        JUCE_MODAL_LOOPS_PERMITTED is 0, so a test cannot run a dispatch loop to
        let a Timer fire. Exposing the clock is the idiom the status bar already
        uses, and what it leaves under test is the part that is ours: that ten
        edits coalesce into ONE check rather than ten.
    */
    void flushPendingCheck();

    juce::CodeDocument& getSourceDocument()
    {
        return source;
    }
    juce::CodeEditorComponent& getEditor()
    {
        return editor;
    }

    /** Puts the caret on a diagnostic and scrolls it into view. */
    void showDiagnostic (int index);

    /** Where a byte offset lands, counted in CHARACTERS.

        The compiler reports byte offsets, which is right for a caret printed
        under a line; juce::CodeDocument::Position counts characters. They
        diverge at the first multi-byte character - one em dash in a comment
        puts every squiggle after it two columns right - so the conversion
        happens once, here, with a test on non-ASCII source.
    */
    static int characterIndexForByte (const std::string& utf8, std::uint32_t byteOffset);

    /** What the tab shows when a project has no score yet.

        Offered, not stored: it becomes part of the project the moment it is
        edited or compiled, and not before.
    */
    static juce::String starterScore();

    /** The inverse: a character index back to the byte offset the compiler
        speaks in. The caret is a character position; completion is asked in
        bytes.
    */
    static int byteIndexForCharacter (const std::string& utf8, int characterIndex);

    // --- completion ----------------------------------------------------------
    /** Offers what may be written at the caret. Control-Space, and a test. */
    void showCompletions();

    void hideCompletions();

    /** Replaces the partial word under the caret with the selected candidate. */
    void acceptCompletion();

    bool isCompletionVisible() const;

    ScoreCompletionList& getCompletionList()
    {
        return completions;
    }

    // --- text size -----------------------------------------------------------
    /** Which rung of the code scale the editor draws at.

        The score is the one place in dew whose text size the reader chooses,
        and the reason is that it is the one place there is a DOCUMENT: it is
        read for minutes at a time, where a panel is glanced at. Everything
        else answers to the interface scale in the View menu.

        Clamped here rather than in Settings, which stores it: the rungs are a
        design-system fact and dew_app cannot see dew_design.
    */
    void setFontStep (int);
    int getFontStep() const noexcept
    {
        return fontStep;
    }

    static int numFontSteps() noexcept;
    static int defaultFontStep() noexcept;

    /** Keys reach here BEFORE the editor sees them, which is the only way a
        popup can own Up, Down, Return and Escape while it is open without
        subclassing CodeEditorComponent and owning its layout too.

        Public, unlike the other overrides, because it is the seam: a test
        asserting that alt-`=` sizes the text and a bare `=` does not has to
        take the path a real press takes, and there is no second door onto it.
    */
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    // Component declares a one-argument keyPressed and KeyListener a
    // two-argument one; without this the second hides the first, which
    // -Woverloaded-virtual rejects and which would silently stop this component
    // ever receiving a key of its own.
    using juce::Component::keyPressed;

    std::function<void (const juce::String&, StatusBar::Severity)> onMessage;

private:
    /** Applies fontStep to the editor.

        The diagnostics list is deliberately not scaled with it: its rows are
        controlHeightSm tall, so text that grew with the document would be
        clipped by a list that did not.
    */
    void applyFontStep();
    void codeDocumentTextInserted (const juce::String&, int) override;
    void codeDocumentTextDeleted (int, int) override;
    void timerCallback() override;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    /** Writes the text into the project. One transaction per typing run. */
    void storeSource();

    void say (const juce::String&, StatusBar::Severity);

    ProjectDocument& document;

    juce::CodeDocument source;
    ScoreTokeniser tokeniser;
    ScoreTextEditor editor { source, &tokeniser };

    DiagnosticsOverlay overlay { *this };
    juce::ListBox list { "scoreDiagnostics", this };

    ScoreCompletionList completions;

    /** What the offered list is replacing: the partial word under the caret at
        the moment it was offered.

        Held, because acceptCompletion needs it and the only other way to get it
        is to ask completionsAt again - which tokenizes the whole document,
        parses it and resolves it, for one range the first call already had. An
        accepted completion did that work twice.

        Set beside setItems and cleared with the list, so it can never describe
        a document that has moved on: nothing edits the score between offering a
        completion and accepting one, because the popup has the keys.
    */
    lang::SourceRange completionReplacing;
    DewButton compileButton { tr (StringId::score_compile_label), DewButton::Role::primary };
    juce::Label heading;

    std::vector<lang::Diagnostic> diagnostics;
    std::string checkedText;

    /** The text the project holds as far as this editor knows, so refresh can
        tell "the document was replaced" from "I wrote that".
    */
    juce::String mirrored;

    int checkCount = 0;
    int fontStep = 0; ///< seeded by applyFontStep in the constructor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreEditorComponent)
};

} // namespace dew
