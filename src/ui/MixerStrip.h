#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Ids.h"
#include "model/ModuleCatalog.h"

#include "app/ProjectDocument.h"
#include "ui/ParamContextMenu.h"
#include "ui/design/Icons.h"
#include "ui/primitives/DewButtons.h"
#include "ui/primitives/DewKnob.h"
#include "ui/primitives/HoverTracker.h"
#include "ui/primitives/RotaryGesture.h"
#include "ui/design/Gestures.h"
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

    /** The effect badge counts EFFECT children, so the strip has to hear about
        them. It listened to the track and implemented only the property hook,
        which meant the count it painted was whatever it had been when
        something unrelated last caused a repaint - hovering the strip, muting
        it, selecting another one. Adding an effect changed the band below and
        left the badge above it stale. */
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override;

    /** Repaints when `child` is an effect of THIS track. Shared by the two
        hooks above so they cannot come to disagree about what counts. */
    void repaintForEffectChange (const juce::ValueTree& child);

    /** The toggle's state and the strip's dimming, together, because they are
        one fact and used to be set at two call sites that disagreed: the
        constructor lit the toggle and the property change lit the toggle, and
        neither of them dimmed anything. */
    void applyMuteState();

    void paintMeter (juce::Graphics&);
    void paintRouting (juce::Graphics&);

    /** The name row. It was a bare 18 in resized(), which is also where the
        badge now takes its slot - the two have to agree about how tall the row
        is, so they read it from one place. */
    static constexpr int nameRowHeight = 18;

    static constexpr int meterWidth = 8;
    static constexpr int routingHeight = 66;

    /** One row of the routing list. The painter and the hit test both need it
        and both had it written out, which is a click that selects the wrong
        channel the moment one of them changes.

        Fourteen rather than twelve, because the name inside it moved up a rung:
        an eleven-point name in a twelve-pixel row was the smallest text in the
        window and it is a LINK - clicking it selects that channel and switches
        tab. routingHeight goes with it so the same four rows still fit. */
    static constexpr int routingRowHeight = 14;
    static constexpr int tickIntervalMs = 1000 / tokens::motion::uiRefreshHz;

    ProjectDocument& document;
    juce::ValueTree track;

    bool isMaster;
    bool selected = false;
    HoverTracker hover { *this };

    float level = 0.0f;
    float lastPaintedLevel = -1.0f;

    juce::Rectangle<int> meterBounds, routingBounds;

    /** Where the effect-count badge goes, and empty when there is nothing to
        count. LAID OUT rather than painted at an offset from the strip's right
        edge: it was four bare floats putting a 16px block at x 50..66 of a
        72px strip, straight through the name label - which, being a child,
        painted over it. A slot the name row gives up is a slot the name row
        knows it has given up. */
    juce::Rectangle<int> badgeBounds;
    juce::Array<juce::var> routedNames;
    juce::Array<juce::Colour> routedColours;
    juce::Array<int> routedIds;

    /** One gesture for the strip: only one control can be under the pointer at
        a time, so the fader and the pan knob share it. */
    RotaryGesture gesture;

    juce::Label nameLabel;
    /** DewSlider rather than juce::Slider: a right-drag on a fader used to
        arm the drag branch and write an undo step. See DewSlider. */
    DewSlider gainSlider;

    /** Latches shift onto the fader at the moment of press.

        The fader is the one value control in dew with no primitive of its own,
        so it is also the one that had to be told the gesture rules by hand -
        and it never was. "Shift is finer, on every knob, fader and number
        field" was written down and true of two of the three: a bare
        juce::Slider snaps to the pointer, which is a position rather than a
        drag, so neither the shared distance nor shift reached it.

        A listener rather than MixerStrip::mouseDown, because that one reads
        event.getPosition() against routingBounds and a forwarded event carries
        the position in the SLIDER's coordinates - the routing rows would answer
        a press on the fader.
    */
    struct FineDrag : juce::MouseListener
    {
        explicit FineDrag (juce::Slider& s)
            : slider (s)
        {
        }

        void mouseDown (const juce::MouseEvent& event) override
        {
            // A right press is not a drag, and must not decide how far one
            // travels: the fader refuses it outright, so the sensitivity this
            // would leave behind is the sensitivity the NEXT drag inherits.
            if (event.mods.isPopupMenu())
                return;

            slider.setMouseDragSensitivity (gesture::dragPixelsFor (event.mods));
        }

        juce::Slider& slider;
    };

    FineDrag fineDrag { gainSlider };

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

    /** Whether the insert plays. One state, the same control the playlist and
        the channel rack now carry, and the same shift-click. See
        PlaylistTrackHeader::enabledButton for why there is one and not two. */
    DewIconButton enabledButton { icons::power(), {} };

    /** Owned here, and destroyed before the controls they watch. */
    std::vector<std::unique_ptr<paramMenu::Trigger>> paramMenuTriggers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrip)
};

} // namespace dew
