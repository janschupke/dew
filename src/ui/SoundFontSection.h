#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

#include "io/SoundFontPool.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "app/ProjectDocument.h"
#include "ui/ParamContextMenu.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** A soundfont channel's file, the sound chosen inside it, and the offsets.

    The third face of InstrumentPanel, and deliberately the same shape as
    SampleSection: one setOwner(), one static requiredHeight the host budgets
    against, one write() opening exactly one undo transaction per gesture, and a
    listener filtered by node identity rather than node type because it listens
    to the whole document.

    A synth channel is edited by describing the sound you want and an audio
    channel by pointing at the part of a recording you meant. A soundfont
    channel is edited by choosing somebody else's sound and then bending it, so
    what is on show is the file, the preset, and six offsets - never a value
    that would overwrite what the font's author decided per region.
*/
class SoundFontSection : public juce::Component, private juce::ValueTree::Listener
{
public:
    /** Hands this section's knobs what a right-click menu needs. Null means no
        menus. None of these is automatable yet, so what the menu offers is a
        reset - which is the other half of why a control has one. */
    void setParamMenuHost (const paramMenu::Host*);

    /** @param pool  where the font is read from. Null shows the empty state,
                      which is what lets a test or dew_shot build the panel with
                      no pool at all. */
    SoundFontSection (ProjectDocument&, SoundFontPool* pool);
    ~SoundFontSection() override;

    void setOwner (juce::ValueTree soundFontNode);

    /** Height this section needs, as a constant for the reason
        SampleSection::requiredHeight is: the host budgets for it before
        anything has been laid out. */
    static constexpr int requiredHeight = tokens::size::controlHeight // the file row
                                          + tokens::space::sm
                                          + tokens::size::controlHeight // the preset row
                                          + tokens::space::sm + tokens::size::knobRow
                                          + tokens::space::sm + tokens::size::knobRow;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    // --- for tests -----------------------------------------------------------

    /** What the font holds, as the dropdown offers it. Empty when no font is
        loaded, which is what the empty state draws instead. */
    juce::StringArray presetMenuItems() const;

    /** Chooses the nth preset the dropdown offers, as clicking it would. */
    bool applyPresetChoice (int oneBasedChoice);

    /** Points the channel at a file, as the Load button's chooser would. A seam
        rather than a chooser, because a file chooser cannot be driven headlessly
        - see MenuSeam.h for the same argument about menus. */
    void loadFile (const juce::File&);

    juce::String getFileDescription() const;

    DewButton& getLoadButton() noexcept
    {
        return loadButton;
    }
    DewDropdown& getPresetBox() noexcept
    {
        return presetBox;
    }
    DewKnob& getTransposeKnob() noexcept
    {
        return transposeKnob;
    }

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    void write (const juce::Identifier& property, const juce::var& value,
                const juce::String& transactionName);

    void attachKnob (DewKnob&, const juce::Identifier& property,
                     const juce::String& transactionName);

    void chooseFile();

    const SoundFontPool::Entry* entry() const;

    ProjectDocument& document;
    SoundFontPool* pool;

    juce::ValueTree soundFont;

    const paramMenu::Host* paramMenuHost = nullptr;

    juce::Label fileLabel;
    DewButton loadButton { tr (StringId::soundfont_load_label) };
    DewDropdown presetBox;
    juce::Label presetLabel;

    DewKnob transposeKnob { requireInstrumentParamSpec (ids::transpose) };
    DewKnob tuneKnob { requireInstrumentParamSpec (ids::tuneCents) };
    DewKnob filterKnob { requireInstrumentParamSpec (ids::filterOffset) };
    DewKnob attackKnob { requireInstrumentParamSpec (ids::attackScale) };
    DewKnob releaseKnob { requireInstrumentParamSpec (ids::releaseScale) };
    DewKnob velocityKnob { requireInstrumentParamSpec (ids::velocitySens) };

    /** The chooser, kept alive across its own callback. JUCE_MODAL_LOOPS_PERMITTED
        is 0, so this is the async form; the blocking one asserts. */
    std::unique_ptr<juce::FileChooser> chooser;

    bool inDrag = false;
    bool gestureActive = false;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundFontSection)
};

} // namespace dew
