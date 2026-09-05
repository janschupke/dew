#include "ui/primitives/DewSearchField.h"

#include "ui/design/Cursors.h"

namespace dew
{

using namespace tokens;

DewSearchField::DewSearchField()
{
    setComponentID ("searchField");

    // Transparent and borderless, so the wrapper's paint is the only thing
    // drawing a field. The two colours it still needs are the look and feel's
    // own ColourIds, not copies, so a theme change carries them.
    editor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);

    editor.setFont (type::font (type::body));
    editor.setMultiLine (false);
    editor.setReturnKeyStartsNewLine (false);
    editor.setBorder ({});

    // The LEFT indent is JUCE's own default, so nothing moves sideways. The top
    // one has to go: setTextToShowWhenEmpty centres the placeholder inside a
    // rectangle whose top has already been trimmed by topIndent, while the text
    // that replaces it compensates for the trim and centres properly - so the
    // two did not sit on the same line, and the placeholder read as two pixels
    // low in every search box in the application.
    editor.setIndents (space::xs, 0);
    editor.setJustification (juce::Justification::centredLeft);
    editor.setMouseCursor (cursor::value);

    // Every keystroke, not just return: a filter that only applies when you
    // commit it is a filter you have to remember to commit.
    editor.onTextChange = [this]
    {
        updateClearButton();
        repaint();

        if (onTextChange != nullptr)
            onTextChange (editor.getText());
    };

    // Escape empties the field, and the dialog around it still closes on the
    // next one. Whether the key is CONSUMED is what makes both true, and
    // updateClearButton keeps it in step with what is typed: a full field
    // swallows escape and clears, an empty one lets it through to the dialog.
    // Without that a search box would take the only key that closes a dialog
    // and give nothing back once it was already empty.
    editor.onEscapeKey = [this] { setText ({}, juce::sendNotification); };

    addAndMakeVisible (editor);

    clearButton.onClick = [this]
    {
        setText ({}, juce::sendNotification);
        editor.grabKeyboardFocus();
    };

    addChildComponent (clearButton);

    updateClearButton();
}

juce::String DewSearchField::getText() const
{
    return editor.getText();
}

void DewSearchField::setText (const juce::String& text, juce::NotificationType notify)
{
    editor.setText (text, notify);

    // TextEditor::setText only fires onTextChange when it is told to, and it is
    // silent when the text has not changed - so the button is brought into step
    // here rather than left to a callback that may not run.
    updateClearButton();
    repaint();
}

void DewSearchField::setPlaceholder (const juce::String& text)
{
    placeholder = text;
    lookAndFeelChanged();
}

void DewSearchField::setClearTooltip (const juce::String& text)
{
    clearButton.setTooltip (text);
}

void DewSearchField::lookAndFeelChanged()
{
    editor.setTextToShowWhenEmpty (placeholder, colour::textDisabled);
}

void DewSearchField::setTooltip (const juce::String& text)
{
    SettableTooltipClient::setTooltip (text);
    editor.setTooltip (text);

    // The name a screen reader reads, on the editor, because that is what
    // carries the role - the wrapper's own handler is ignored.
    editor.setTitle (text);
    setTitle (text);
}

std::unique_ptr<juce::AccessibilityHandler> DewSearchField::createAccessibilityHandler()
{
    return createIgnoredAccessibilityHandler (*this);
}

void DewSearchField::updateClearButton()
{
    clearButton.setVisible (! editor.isEmpty());
    editor.setEscapeAndReturnKeysConsumed (! editor.isEmpty());
}

void DewSearchField::paint (juce::Graphics& g)
{
    const auto body = paint::bodyRect (*this);

    paint::wellBackground (g, getLocalBounds());

    g.setColour (colour::divider);
    g.drawRoundedRectangle (body, radius::sm, stroke::hairline);

    // The glyph is chrome rather than content: it says what the box is for and
    // must not compete with what has been typed into it.
    auto glyph = getLocalBounds().removeFromLeft (size::iconButton).toFloat();
    icons::draw (g, icons::search(), glyph.reduced ((float) space::sm), colour::textSecondary);

    // The editor has no peer of its own to ring, and its focus is the field's.
    paint::focusRing (g, *this, editor.hasKeyboardFocus (true));
}

void DewSearchField::resized()
{
    auto area = getLocalBounds();

    area.removeFromLeft (size::iconButton);
    clearButton.setBounds (area.removeFromRight (size::iconButton));

    editor.setBounds (area.withTrimmedRight (space::xs));
}

} // namespace dew
