#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Icons.h"
#include "ui/primitives/DewButtons.h"
#include "ui/primitives/DewKnob.h"

namespace dew
{

/** A box you type a filter into.

    A juce::TextEditor with the field drawn AROUND it rather than by it. The
    editor is made transparent and borderless and the wrapper paints the whole
    thing - the well, its edge, the magnifier and the focus ring - because the
    glyph and the clear button sit inside what reads as one control, and a
    TextEditor painting its own background would put an edge between them.

    A primitive rather than a TextEditor styled where it is used. Every control
    in dew is one, `dew_shot gallery` is how they are reviewed, and a field
    themed at its call site is the second vocabulary the design gates exist to
    refuse.

    The wrapper returns an IGNORED accessibility handler, as DewKnob does: the
    editor inside carries the role, the value and the caret, and switching the
    wrapper off with setAccessible (false) would take the editor off with it,
    because Component::isAccessible walks up to its parent.
*/
class DewSearchField : public juce::Component, public juce::SettableTooltipClient
{
public:
    DewSearchField();

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::String getText() const;

    /** @param notify  sendNotification calls onTextChange, the way a
                       juce::TextEditor's own setText does. */
    void setText (const juce::String&, juce::NotificationType notify);

    /** The word shown in the empty field. Not a label: it disappears the moment
        anybody types, so it says what to do rather than what this is - and the
        tooltip, which does not disappear, is what a screen reader reads. */
    void setPlaceholder (const juce::String&);

    /** What the clear button says it does.

        Taken from the caller rather than written here, because this primitive
        is dew_design's and knows nothing about the surface it is filtering -
        the same reason DewButton takes its text. Without one the button has no
        tooltip and no accessible name, which is a gate failure and, before that,
        a button a screen reader meets in silence.
    */
    void setClearTooltip (const juce::String&);

    /** Called on every keystroke, and by setText with sendNotification. */
    std::function<void (const juce::String&)> onTextChange;

    /** Sets the tooltip AND the accessible name, on this and on the editor
        inside it - juce::TooltipWindow hit-tests the DEEPEST component under
        the pointer, so a tooltip only on the wrapper is a tooltip nobody sees.
        The same trap DewKnob documents.
    */
    void setTooltip (const juce::String&) override;

    /** The editor, so a test can type into the thing a person types into. */
    juce::TextEditor& getEditor() noexcept
    {
        return editor;
    }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    /** The placeholder's colour is a COPY, taken when it is set, so it is taken
        again whenever the palette moves - the rule every primitive that holds a
        colour rather than reading one in paint() follows. */
    void lookAndFeelChanged() override;

    /** Shows and hides the clear button, which has nothing to do while the
        field is empty and is one more thing to tab past. */
    void updateClearButton();

    juce::TextEditor editor;
    DewIconButton clearButton { icons::cross(), {} };

    /** Kept so lookAndFeelChanged can set it again in the new palette.
        setTextToShowWhenEmpty takes a colour and a string together, and there
        is no way to ask a TextEditor what it was given. */
    juce::String placeholder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewSearchField)
};

} // namespace dew
