#include "ui/ScoreEditorComponent.h"

#include <algorithm>

#include "lang/Completion.h"
#include "lang/SourceRange.h"
#include "ui/ScoreEditorLayout.h"
#include "ui/ScoreLocale.h"

/*  The completion popup, and the byte-versus-character arithmetic under it.

    Split out because ScoreEditorComponent.cpp reached the length gate, and this
    is the seam that was already there: the popup is a self-contained thing with
    its own list, its own visibility and its own idea of where the caret is.

    The two index helpers belong with it rather than with the editor. The score
    language reports a position as a BYTE offset into UTF-8 and a
    juce::CodeDocument counts CHARACTERS, so every crossing between the two
    happens here - and it only happens because of completions.
*/

namespace dew
{

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
    completionReplacing = {};
}

void ScoreEditorComponent::showCompletions()
{
    const auto text = source.getAllContent().toStdString();
    const auto offset = byteIndexForCharacter (text, editor.getCaretPos().getPosition());

    const auto result = lang::completionsAt (text, (std::uint32_t) offset, scoreLocale());

    if (result.items.empty())
    {
        hideCompletions();
        return;
    }

    completions.setItems (result.items);
    completionReplacing = result.replacing;

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

    // The partial word is REPLACED, not appended to, or accepting `channel`
    // after `cha` spells `chachannel`. The range is the one showCompletions
    // already resolved - asking completionsAt again tokenized the document a
    // second time, parsed it a second time and resolved it a second time, to
    // recover two numbers.
    const juce::CodeDocument::Position from { source, completionReplacing.isEmpty()
                                                          ? editor.getCaretPos().getPosition()
                                                          : characterIndexForByte (
                                                                text, completionReplacing.begin) };

    const juce::CodeDocument::Position to { source, editor.getCaretPos().getPosition() };

    hideCompletions();

    source.replaceSection (from.getPosition(), to.getPosition(), juce::String (selected->text));

    editor.moveCaretTo (
        juce::CodeDocument::Position (source, from.getPosition() + (int) selected->text.size()),
        false);
    editor.grabKeyboardFocus();
}

} // namespace dew
