#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"

namespace dew
{

/** Double-click a value and type an exact one.

    Written for DewNumberField and then wanted by DewKnob, which had no way of
    being GIVEN a number at all: a knob's readout is drawn text rather than a
    control, so the only route to an exact value was to drag until the number
    happened to say it.

    Extracted rather than copied, because the parts that go wrong when this is
    written twice are the parts that are easy to get almost right: there are
    three ways out of the box and they do not agree - return keeps, escape does
    not, and losing focus keeps - and the text has to be a PLAIN number. A
    readout carries its unit, and a box seeded with "0.140 s" is a box whose
    contents do not parse.

    Header-only and JUCE-only, like the rest of dew_design: it knows what an
    input looks like and nothing about what the number means.
*/
class TypedEdit
{
public:
    explicit TypedEdit (juce::Component& c)
        : owner (c)
    {
    }

    bool isActive() const noexcept
    {
        return editor != nullptr;
    }

    /** Opens the box over `bounds`, in the owner's own coordinates.

        @param value          what to seed it with - without a unit
        @param decimalPlaces  how the seed is written, so re-typing what is
                              already there is a no-op rather than a rounding
        @param outline        the edge colour: the control's function colour,
                              which is what says the box is live
        @param commit         called with the typed number, and only when the
                              box is left in a way that keeps it
    */
    void begin (juce::Rectangle<int> bounds, double value, int decimalPlaces, juce::Colour outline,
                std::function<void (double)> commit)
    {
        if (editor != nullptr)
            return;

        editor = std::make_unique<juce::TextEditor>();
        editor->setBounds (bounds);
        editor->setJustification (juce::Justification::centred);
        editor->setFont (tokens::type::font (tokens::type::body));
        editor->setText (juce::String (value, decimalPlaces), false);
        editor->setSelectAllWhenFocused (true);
        editor->setColour (juce::TextEditor::backgroundColourId, tokens::colour::well);
        editor->setColour (juce::TextEditor::textColourId, tokens::colour::textPrimary);
        editor->setColour (juce::TextEditor::outlineColourId, outline);

        onCommit = std::move (commit);

        // Only `this` is captured, and that is load-bearing rather than tidy:
        // finish() destroys the editor, which destroys the very std::function
        // it is running inside, so anything the lambda had CAPTURED would be
        // gone before it could be used. The commit callback is a member of this
        // for exactly that reason - it outlives the box.
        editor->onReturnKey = [this] { finish (true); };
        editor->onEscapeKey = [this] { finish (false); };
        editor->onFocusLost = [this] { finish (true); };

        owner.addAndMakeVisible (*editor);
        editor->grabKeyboardFocus();
    }

    /** Keeps the box on the bounds it was opened over, when the owner is laid
        out again while it is up. */
    void setBounds (juce::Rectangle<int> bounds)
    {
        if (editor != nullptr)
            editor->setBounds (bounds);
    }

private:
    /** The three ways out of the box, which do not agree: return keeps, escape
        does not, and losing focus keeps. */
    void finish (bool keep)
    {
        if (editor == nullptr)
            return;

        // The text comes off it BEFORE the reset, and nothing after the reset
        // reads anything the destroyed editor owned - see the capture above.
        const auto text = editor->getText();
        editor.reset();

        if (keep && text.isNotEmpty() && onCommit != nullptr)
            onCommit (text.getDoubleValue());

        owner.repaint();
    }

    juce::Component& owner;
    std::unique_ptr<juce::TextEditor> editor;
    std::function<void (double)> onCommit;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TypedEdit)
};

} // namespace dew
