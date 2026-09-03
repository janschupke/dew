#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <string>
#include <vector>

#include "lang/Compile.h"
#include "model/ProjectDocument.h"
#include "ui/ScoreTokeniser.h"
#include "ui/StatusBar.h"
#include "ui/primitives/DewControls.h"

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
class DiagnosticsOverlay : public juce::Component,
                           private juce::Timer
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
class ScoreEditorComponent : public juce::Component,
                             private juce::CodeDocument::Listener,
                             private juce::Timer,
                             private juce::ListBoxModel
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

    const std::vector<lang::Diagnostic>& getDiagnostics() const { return diagnostics; }

    /** How many times the text has been checked. A test types ten characters,
        advances the clock once, and asserts this went up by one.
    */
    int getCheckCount() const { return checkCount; }

    /** True while an edit is waiting for typing to stop. */
    bool isCheckPending() const { return isTimerRunning(); }

    /** Fires the pending check now, as if typing had paused.

        JUCE_MODAL_LOOPS_PERMITTED is 0, so a test cannot run a dispatch loop to
        let a Timer fire. Exposing the clock is the idiom the status bar already
        uses, and what it leaves under test is the part that is ours: that ten
        edits coalesce into ONE check rather than ten.
    */
    void flushPendingCheck();

    juce::CodeDocument& getSourceDocument() { return source; }
    juce::CodeEditorComponent& getEditor() { return editor; }

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

    std::function<void (const juce::String&, StatusBar::Severity)> onMessage;

private:
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
    juce::CodeEditorComponent editor { source, &tokeniser };

    DiagnosticsOverlay overlay { *this };
    juce::ListBox list { "scoreDiagnostics", this };

    DewButton compileButton { "Compile", DewButton::Role::primary };
    juce::Label heading;

    std::vector<lang::Diagnostic> diagnostics;
    std::string checkedText;

    /** The text the project holds as far as this editor knows, so refresh can
        tell "the document was replaced" from "I wrote that".
    */
    juce::String mirrored;

    int checkCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreEditorComponent)
};

} // namespace dew
