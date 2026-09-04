#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Ids.h"
#include "model/ModuleCatalog.h"

#include "app/ProjectDocument.h"
#include "ui/ParamContextMenu.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/HoverTracker.h"
#include "ui/design/Tokens.h"

namespace dew
{

/** One mixer strip: a fader, a meter, pan, mute, solo, the channels routed into
    it, and a badge saying how many effects it carries.

    Master has no pan, mute or solo, so it is the same class with those controls
    hidden rather than a second nearly-identical one.

    A nested class of MixerComponent until it was 62% of that file. Promoted for
    the reason PlaylistTrackHeader and ChannelRackHeader were: it reads none of
    the mixer's members. Adding and removing an insert are the MIXER's edits -
    the strip is only the surface the menu opened on, and is deleted by the
    rebuild either one causes - so it reports them and touches nothing after.

    It is deliberately NOT a HeaderRow. It holds a fader rather than a name and
    two toggles, and it is a column rather than a row; what it borrows from that
    base is the three-line menu seam, which it states rather than inherits.
*/
class MixerStrip : public juce::Component, private juce::ValueTree::Listener
{
public:
    MixerStrip (ProjectDocument&, juce::ValueTree, bool isMasterStrip);
    ~MixerStrip() override;

    void setSelected (bool shouldBeSelected);

    /** New peak from the engine, 0..1. Falls back smoothly rather than snapping,
        so a meter reads as a level and not as a flicker.
    */
    void setLevel (float peak);

    /** Names of the channels routed into this strip, with their colours and
        ids, so a routing row is a way to reach the channel it names.
    */
    void setRouting (juce::Array<juce::var> names, juce::Array<juce::Colour> colours,
                     juce::Array<int> ids);

    std::function<void (int channelId)> onChannelClicked;

    bool isMasterStrip() const noexcept
    {
        return isMaster;
    }

    int getTrackId() const
    {
        return (int) track[ids::id];
    }

    std::function<void()> onSelected;

    /** Adding and removing are the MIXER's edits, not a strip's: the strip is
        the surface the menu opened on and is deleted by the rebuild either one
        causes, so it reports the gesture and touches nothing afterwards. */
    std::function<void()> onAddInsert;
    std::function<void (int trackId)> onRemoveInsert;

    void select();

    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    // --- the menu ------------------------------------------------------------
    // Built and applied by named methods rather than by a lambda inside
    // showMenuAsync, for the reason MenuSeam.h gives: showMenuAsync cannot be
    // driven headlessly, so a test reads the built menu instead.
    enum class MenuItem
    {
        rename = 1,
        addInsert,
        removeInsert
    };

    static constexpr int colourBaseId = (int) MenuItem::removeInsert + 1;

    juce::PopupMenu buildMenu() const;
    void applyMenuChoice (int choice);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The controls on this strip that were built from a ParamSpec.

        The master strip has a fader and nothing else, so its pan and its
        toggles are not built at all - attachTo on a control that does not exist
        would be a crash, which is why each of these is guarded by the same flag
        that built it.
    */
    void attachParamMenus (const paramMenu::Host* host);

private:
    void showMenu (const juce::MouseEvent&);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override;

    void paintMeter (juce::Graphics&);
    void paintRouting (juce::Graphics&);

    static constexpr int meterWidth = 8;
    static constexpr int routingHeight = 58;

    /** One row of the routing list. The painter and the hit test both need it
        and both had it written out, which is a click that selects the wrong
        channel the moment one of them changes. */
    static constexpr int routingRowHeight = 12;
    static constexpr int tickIntervalMs = 1000 / tokens::motion::uiRefreshHz;

    ProjectDocument& document;
    juce::ValueTree track;

    bool isMaster;
    bool selected = false;
    HoverTracker hover { *this };

    float level = 0.0f;
    float lastPaintedLevel = -1.0f;

    juce::Rectangle<int> meterBounds, routingBounds;
    juce::Array<juce::var> routedNames;
    juce::Array<juce::Colour> routedColours;
    juce::Array<int> routedIds;

    /** True between a fader's onDragStart and onDragEnd. One flag for the strip:
        only one control can be under the pointer at a time.
    */
    bool inDrag = false;
    bool gestureActive = false;

    juce::Label nameLabel;
    juce::Slider gainSlider;

    /** The same two controls the channel rack's rows carry, at the same size and
        in the same painter.

        These were a bare rotary juce::Slider at 38px and two juce::TextButtons,
        which is a third knob size and a second idea of what a mute letter looks
        like - in the one view whose whole job is to sit beside the others and
        agree with them. They also had no right-click menu of their own, which
        the comment beside attachParamMenus used to state as a cost worth
        paying; carrying it is what pays it back.
    */
    DewKnob panKnob { requireMixerTrackParamSpec (ids::pan) };
    DewLetterToggle muteButton { "M", tokens::colour::warning, "Mute this track" };
    DewLetterToggle soloButton { "S", tokens::colour::accent, "Solo this track" };

    /** Owned here, and destroyed before the controls they watch. */
    std::vector<std::unique_ptr<paramMenu::Trigger>> paramMenuTriggers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrip)
};

} // namespace dew
