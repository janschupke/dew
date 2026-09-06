#include "ui/primitives/DewReadOnlyField.h"

#include "ui/design/Cursors.h"
#include "ui/primitives/DewPaint.h"

namespace dew
{

using namespace tokens;

DewReadOnlyField::DewReadOnlyField()
{
    setComponentID ("readOnlyField");

    // Transparent and borderless, so the wrapper's paint is the only thing
    // drawing a field - the same arrangement DewSearchField documents.
    editor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);

    editor.setFont (type::font (type::body));
    editor.setMultiLine (false);
    editor.setBorder ({});
    editor.setIndents (space::xs, 0);
    editor.setJustification (juce::Justification::centredLeft);

    // Read-only rather than disabled: a disabled editor cannot be selected
    // from, which would leave this exactly as useful as the drawn text it
    // replaces. The caret goes because there is nothing to type - the
    // SELECTION stays, and that is the whole feature.
    editor.setReadOnly (true);
    editor.setCaretVisible (false);
    editor.setPopupMenuEnabled (true);
    editor.setMouseCursor (cursor::text);

    addAndMakeVisible (editor);

    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (editor.getText());

        // Selected as well as copied, so the button SHOWS what it took. A
        // clipboard write is otherwise the one action in the application with
        // no visible result at all.
        editor.selectAll();
        editor.grabKeyboardFocus();
    };

    addAndMakeVisible (copyButton);
}

void DewReadOnlyField::setText (const juce::String& text)
{
    editor.setText (text, juce::dontSendNotification);
    repaint();
}

juce::String DewReadOnlyField::getText() const
{
    return editor.getText();
}

void DewReadOnlyField::setFont (const juce::Font& font)
{
    editor.setFont (font);

    // applyFontToAllText as well: setFont is what the editor uses for text
    // arriving AFTER it, and a field whose text was set first would keep the
    // old one.
    editor.applyFontToAllText (font);
}

void DewReadOnlyField::setCopyTooltip (const juce::String& text)
{
    copyButton.setTooltip (text);
}

void DewReadOnlyField::setCopyable (bool canCopy)
{
    copyButton.setVisible (canCopy);
    resized();
}

void DewReadOnlyField::setTooltip (const juce::String& text)
{
    SettableTooltipClient::setTooltip (text);
    editor.setTooltip (text);

    editor.setTitle (text);
    setTitle (text);
}

std::unique_ptr<juce::AccessibilityHandler> DewReadOnlyField::createAccessibilityHandler()
{
    return createIgnoredAccessibilityHandler (*this);
}

void DewReadOnlyField::paint (juce::Graphics& g)
{
    // The well says "text lives here", which is true of something you are
    // meant to take out of it as much as of something you type into it.
    paint::wellBackground (g, getLocalBounds());
    paint::inputBox (g, *this, juce::Colours::transparentBlack, colour::outline);
    paint::focusRing (g, *this, editor.hasKeyboardFocus (true));
}

void DewReadOnlyField::resized()
{
    auto area = getLocalBounds();

    // Only reserved when it is there: a field with nothing to copy gives the
    // width back to the text rather than keeping a hole where the button was.
    if (copyButton.isVisible())
        copyButton.setBounds (area.removeFromRight (size::iconButton));

    editor.setBounds (area.withTrimmedRight (space::xs));
}

} // namespace dew
