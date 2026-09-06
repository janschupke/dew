#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

#include "model/NoteTools.h"
#include "ui/DewDialog.h"
#include "ui/primitives/DewButtons.h"
#include "ui/primitives/DewNumberField.h"

namespace dew
{

/** How much to disturb the notes an edit is about to act on.

    A one-shot edit, not a playback-time effect: pressing Randomize rewrites the
    notes once and the result is on the undo stack like any other edit.

    Deliberately no enable toggles beside the amounts. An amount of zero already
    means "leave this alone", and nothing else in dew uses a juce::ToggleButton -
    it would render as JUCE's default tick box in an application whose design
    system exists to avoid exactly that.
*/
class RandomizePanel : public dialog::Panel
{
public:
    /** `scopeText` says what the edit will hit, so the dialog does not have to
        know anything about the document to describe its own effect.
    */
    RandomizePanel (NoteTools::RandomizeOptions initial, juce::String scopeText);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens it in a dialog window owned by JUCE, as the audio settings do. */
    static void show (NoteTools::RandomizeOptions initial, juce::String scopeText,
                      juce::Component* parent,
                      std::function<void (const NoteTools::RandomizeOptions&)> onApply);

    std::function<void (const NoteTools::RandomizeOptions&)> onApply;

    // --- for tests -----------------------------------------------------------
    NoteTools::RandomizeOptions getOptions() const;
    void setOptions (const NoteTools::RandomizeOptions&);

    juce::Button& getApplyButton() noexcept
    {
        return applyButton;
    }
    juce::Button& getCancelButton() noexcept
    {
        return cancelButton;
    }

    static constexpr int preferredWidth = 320;
    static constexpr int preferredHeight = 186;

private:
    juce::String scopeText;

    DewNumberField velocityField;
    DewNumberField stepField;

    DewButton cancelButton { tr (StringId::dialog_cancel), DewButton::Role::ghost };
    DewButton applyButton { tr (StringId::randomize_apply_label), DewButton::Role::primary };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RandomizePanel)
};

} // namespace dew
