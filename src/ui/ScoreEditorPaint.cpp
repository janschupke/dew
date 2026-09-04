// =============================================================================
// The score tab's painting, and the overlay that draws its diagnostics.
//
// The same class's file-local helpers plus one small component, in a second
// translation unit - the shape PianoRollPaint.cpp already uses.
//
// The squiggle, the severity colours and the font ladder are pure functions of
// their arguments; DiagnosticsOverlay is a component whose whole job is to draw
// what the checker found on top of the text without taking a click. Neither has
// anything to do with completion, compiling, or what a keystroke means, which
// is the rest of the tab.
// =============================================================================

#include "ui/ScoreEditorComponent.h"

#include <algorithm>
#include <cmath>

#include "lang/SourceRange.h"
#include "ui/ScoreEditorLayout.h"
#include "ui/design/Tokens.h"

namespace dew
{

namespace
{

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

} // namespace dew
