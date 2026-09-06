#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "ui/ConfirmPanel.h"
#include "ui/EditorState.h"
#include "ui/MixerStrip.h"
#include "ui/ParamContextMenu.h"
#include "ui/EffectChainHost.h"

namespace dew
{

/** Two rows. Along the top, one strip per insert - a fader, a pan knob, mute
    and solo - plus master; along the bottom, the effect chain of whichever
    strip is selected, its effects side by side.

    The chain editor is shared rather than one per strip: a strip is 96px wide,
    which is nowhere near enough to edit a delay in, and a single editor also
    makes it obvious which chain you are looking at.

    It runs across rather than down because that is the shape the space has. A
    column of cards in the bottom of a mixer is a narrow slot with a thousand
    pixels of empty panel beside it, and it was one - the chain used to be
    capped at 430px wide and scrolled inside a 230px porthole.

    Both rows scroll sideways independently, so both carry a scrollbar.
*/
class MixerComponent : public juce::Component,
                       private juce::ValueTree::Listener,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    /** Set from above; null means no automation menus. */
    void setParamMenuHost (const paramMenu::Host* host);

    /** Where a preset row's description goes as the pointer passes over it.

        The status strip answers at once where the floating tooltip waits, which
        is the pair HoverHelp gives every other control - and a popup menu is a
        window of its own, so HoverHelp's own listener cannot reach these rows.
    */
    void setPresetHoverSink (std::function<void (const juce::String&)>);

    /** The engine is optional: it only supplies meter levels, and the mixer is
        constructed without one in tests and in the screenshot tool.
    */
    MixerComponent (ProjectDocument&, EditorState&, AudioEngine* = nullptr);
    ~MixerComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    /** The id the master strip answers to. Real inserts start at 1, so 0 can
        stand for "the master" without colliding with any of them.
    */
    static constexpr int masterTrackId = 0;

    /** Selects a channel and asks whoever owns the tabs to show the rack, so a
        routing entry in the mixer is a way to reach that channel.
    */
    std::function<void()> onShowChannelRack;

    /** What removing an insert asks first. See ConfirmHook. */
    ConfirmHook confirmDestructive;

    /** Appends an insert and selects it, so the shared chain editor follows the
        thing that was just made - the courtesy TransportBar::addPattern does. */
    void addMixerTrack();

    /** Asks, then removes. Also puts the selection somewhere real: that is
        session state, so it is fixed up here rather than inside the edit. */
    void removeMixerTrack (int mixerTrackId);

    /** Drives a strip's menu item without opening a menu, which cannot be done
        headlessly. False when no strip answers to that id; masterTrackId
        reaches the master, which carries no id of its own and so reads as 0. */
    bool applyMixerTrackMenuChoice (int mixerTrackId, int choice);

    /** What a strip's menu offers, for a test to read. */
    juce::StringArray mixerTrackMenuItems (int mixerTrackId) const;

    /** How many strips are showing, master included. */
    int getNumStrips() const noexcept
    {
        return strips.size();
    }

    /** How deep the effect band is, in PIXELS, and the one place it is clamped.

        View geometry lives on the view: not the document, because a band height
        is not music and must neither dirty a project nor land on the undo
        stack, and not EditorState, whose other listeners have no interest in it.
        The precedent is PlaylistComponent's lane height.

        It used to be a number of knob ROWS, on the argument that a height
        between two of them is ground no knob can use. That is true of the
        CARDS and was not true of the drag: rounding the pointer's travel to the
        nearest 68px rung gave the one resizable thing in the application four
        reachable positions, so half a rung of travel did nothing and then it
        jumped a whole one. Every other drag area in dew follows the pointer.

        So the band follows the pointer and the cards keep their whole rows: the
        height is a pixel count, the row budget is knobRowsFitting of it, and
        the few pixels between one rung and the next are band ground rather than
        a knob standing on half a row.

        0 means "never set", which is how the height reaches Settings - the same
        convention a lane height uses, and clamped here for the same reason:
        dew_app cannot see the ladder.
    */
    void setEffectBandHeight (int pixels);
    int getEffectBandHeight() const noexcept
    {
        return effectBandHeight;
    }

    /** The height that shows this many rows. For a caller that thinks in rows -
        a test, and the keyboard nothing has yet - rather than in pixels. */
    void setEffectBandRows (int rows);

    /** How many rows the band is actually showing, which is the budget the
        cards were given rather than a number stored beside it. */
    int getEffectBandRows() const noexcept;

private:
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void rebuildStrips();
    void pointChainAtSelectedTrack();
    void updateRouting();

    /** The strip answering to an id, or null. masterTrackId finds the master,
        which carries no id property and so reads as 0. */
    MixerStrip* stripFor (int mixerTrackId) const;

    /** The band's depth, resolving "never set" to the depth it opens at. */
    int bandHeight() const;

    const paramMenu::Host* paramMenuHost = nullptr;

    ProjectDocument& document;
    EditorState& editorState;
    AudioEngine* engine = nullptr;
    juce::OwnedArray<MixerStrip> strips;
    juce::Viewport stripViewport;
    juce::Component stripHolder;

    /** In the scrolling holder after the last strip, not in a footer: the same
        place the channel rack puts its add button, for the same reason. */
    DewButton addStripButton { tr (StringId::mixer_addInsert_label), DewButton::Role::ghost };

    EffectChainHost chainHost;

    /** In pixels, and 0 until somebody drags it - see setEffectBandHeight. */
    int effectBandHeight = 0;

    /** What the band was when the grip was pressed, so the drag is computed
        from where it started rather than summed sample by sample: two routes to
        the same pointer position have to give the same band.

        An int, and no fractional companion: this delta arrives already in whole
        screen pixels and is measured from the press, so there is nothing here
        for a trackpad's fragments to be lost in - unlike a zoom FACTOR, which
        is why RowView carries an exact height and this does not.
    */
    int heightAtDragStart = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};

} // namespace dew
