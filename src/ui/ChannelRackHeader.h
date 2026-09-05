#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

#include "model/Ids.h"
#include "model/ModuleCatalog.h"

#include "app/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/HeaderRow.h"
#include "ui/ParamContextMenu.h"
#include "ui/design/Icons.h"
#include "ui/primitives/DewControls.h"
#include "ui/design/Tokens.h"

namespace dew
{

/** One channel's header in the rack: colour tab, name, volume, pan, mute, solo,
    and - on an audio channel - the record arm.

    A nested class of ChannelRackComponent until it was nearly half of that
    file. Promoted for the reason PlaylistTrackHeader was: it reads none of the
    rack's members. It takes a document, the editor state and a channel, and
    reports adding and removing - which belong to whoever owns the list -
    through two callbacks.

    Volume and pan are on the row itself so a pattern can be balanced without
    selecting each channel in turn and reaching for the instrument panel.
*/
class ChannelRackHeader : public HeaderRow
{
public:
    ChannelRackHeader (ProjectDocument&, EditorState&, juce::ValueTree);

    int getChannelId() const
    {
        return (int) channel[ids::id];
    }

    void refresh();
    void select();

    enum class MenuItem
    {
        rename = 1,
        addChannel, ///< the row the instrument submenu hangs off; never chosen
        removeChannel,

        // Appended rather than inserted, which is why they sit after remove
        // instead of beside addChannel: the ids above them are already spoken
        // for, here and in the colour submenu's base below.
        addSynth,
        addAudio,
        addSoundFont
    };

    /** Where the colour submenu's ids start: after this row's own, so the two
        numberings cannot collide. */
    static constexpr int colourBaseId = (int) MenuItem::addSoundFont + 1;

    juce::PopupMenu buildMenu() const override;
    void applyMenuChoice (int choice) override;

    /** Add and remove belong to the rack, which owns the list and rebuilds it.

        Add carries the KIND now that the menu offers all three. The rack was
        already the only thing that could make one of each - three buttons under
        the list have done so since before the menu existed - so this asks it
        for the one the menu named rather than teaching the row how.
    */
    std::function<void (InstrumentType)> onAddChannel;
    std::function<void (int channelId)> onRemoveChannel;

    /** Every control on this row that was built from a ParamSpec gets its
        right-click. One place, so a control added to a row cannot quietly be
        the one that has no menu.

        Called again when the host arrives: the rows are built in the rack's
        constructor and the host is set from above afterwards.
    */
    void attachParamMenus (const paramMenu::Host* host);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** Selected on every press, so the menu always acts on the row that was
        clicked rather than on whatever was selected before it. */
    void headerPressed() override
    {
        select();
    }

    juce::Label* editableLabel() override
    {
        return &nameLabel;
    }

    /** Wires one compact knob to one of the channel's properties.

        `onEditStart` selects the row: a knob keeps its own clicks, so the row's
        mouseDown never sees them, and a control that cannot select its row is
        the bug this rack was already fixed for once.
    */
    void attachKnob (DewKnob& knob, const juce::Identifier& property,
                     const juce::String& transactionName, const juce::String& tooltip);

    ProjectDocument& document;
    EditorState& editorState;
    juce::ValueTree channel;

    juce::Label nameLabel;
    juce::Rectangle<int> pitchBounds;
    bool updating = false;
    bool inDrag = false;
    bool gestureActive = false;
    /** Whether the channel plays. One state, the same control the playlist and
        the mixer now carry, and the same shift-click. See
        PlaylistTrackHeader::enabledButton for why there is one and not two. */
    DewIconButton enabledButton { icons::power(), {} };

    DewKnob volumeKnob { requireInstrumentParamSpec (ids::volume) };
    DewKnob panKnob { requireInstrumentParamSpec (ids::pan) };
    DewLetterToggle armButton { "R", tokens::colour::recording,
                                tr (StringId::channelRack_arm_help) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackHeader)
};

} // namespace dew
