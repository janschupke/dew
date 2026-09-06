#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Icons.h"
#include "ui/primitives/DewButtons.h"

namespace dew
{

/** A piece of text the reader has to be able to TAKE, rather than one they
    only have to be able to read.

    An address, a command line - anything whose whole purpose is to end up
    somewhere else. dew drew those with drawFittedText, which produces a string
    that cannot be selected, cannot be copied, and can only be retyped by hand
    from a dialog that is covering the thing you would retype it into.

    The twin of DewSearchField, and built the same way and for the same
    reasons: a juce::TextEditor made transparent and borderless with the
    wrapper painting the field around it, because the copy button sits inside
    what reads as one control and an editor painting its own background would
    put an edge between them. It returns an IGNORED accessibility handler for
    the same reason too - the editor inside carries the role and the selection,
    and switching the wrapper off would take the editor off with it.

    Read-only, but not disabled. That distinction is the point: a disabled
    editor cannot be selected from either, which would leave this no better
    than the drawn text it replaces.
*/
class DewReadOnlyField : public juce::Component, public juce::SettableTooltipClient
{
public:
    DewReadOnlyField();

    void paint (juce::Graphics&) override;
    void resized() override;

    void setText (const juce::String&);

    juce::String getText() const;

    /** Monospaced for a command line, proportional for prose. Taken from the
        caller because this primitive knows nothing about what it is holding. */
    void setFont (const juce::Font&);

    /** What the copy button says it does.

        Taken from the caller, as DewSearchField takes its clear button's, for
        the reason that one is: without it the button has no tooltip and no
        accessible name, which is a gate failure and, before that, a button a
        screen reader meets in silence.
    */
    void setCopyTooltip (const juce::String&);

    /** Whether there is anything here worth taking.

        A field can hold a state rather than a value - "Not running" where an
        address would be - and a button offering to put that on the clipboard
        is a button that does nothing anybody wanted. Hidden rather than
        disabled, so it is not one more thing to tab past either.
    */
    void setCopyable (bool);

    /** Sets the tooltip AND the accessible name, on this and on the editor
        inside it - juce::TooltipWindow hit-tests the DEEPEST component under
        the pointer, so a tooltip only on the wrapper is one nobody sees. The
        same trap DewKnob and DewSearchField both document. */
    void setTooltip (const juce::String&) override;

    /** The editor, so a test can select from the thing a person selects from. */
    juce::TextEditor& getEditor() noexcept
    {
        return editor;
    }

    /** The button, so a test can press what a person presses. */
    DewIconButton& getCopyButton() noexcept
    {
        return copyButton;
    }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::TextEditor editor;

    /** icons::duplicate() rather than an icons::copy() of its own: the shape
        already registered - two offset rounded rectangles - IS the copy glyph
        every toolbar in the world draws, and a second path with the same
        outline would be a new entry in the gallery, a new name to keep unique
        and a new case in the glyph tests, for no pixel of difference. */
    DewIconButton copyButton { icons::duplicate(), {} };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewReadOnlyField)
};

} // namespace dew
