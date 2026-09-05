// =============================================================================
// PlaylistComponent's painting.
//
// The same class, a second translation unit. PlaylistComponent.cpp was 2,200
// lines and about a sixth of it was paint code that shares nothing with the
// gesture handling above it but the members it reads.
//
// Split rather than extracted: these draw from a dozen members each, and a free
// function taking a dozen parameters is not an improvement on a method. What
// this buys is a file about painting and a file about behaviour.
// =============================================================================

#include "i18n/Strings.h"
#include "ui/PlaylistComponent.h"

#include <cmath>
#include <utility>

#include "model/AutomationCurve.h"
#include "model/AutomationTargets.h"
#include "model/ModuleCatalog.h"

#include "ui/AutomationLane.h"

#include "io/SamplePool.h"

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/TimelineRuler.h"
#include "ui/design/Gestures.h"
#include "ui/Hotkeys.h"
#include "ui/ColourMenu.h"
#include "ui/HeaderRow.h"
#include "ui/MenuSeam.h"
#include "ui/TimelinePaint.h"
#include "ui/design/ParamPalette.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

using namespace tokens;

void PlaylistComponent::paintAudioClip (juce::Graphics& g, const juce::ValueTree& clip,
                                        juce::Rectangle<float> bounds, bool audible)
{
    const auto channel = ProjectEdits::findChannel (document.getState(),
                                                    (int) clip[ids::channelId]);

    // The channel's own colour rather than the accent, because an audio clip IS
    // its channel - one recording, one channel - and the arrangement should say
    // which one at a glance, the way the channel rack's colour tabs do.
    auto clipColour = channel.isValid() ? entityColour::of (channel) : colour::textDisabled;

    if (! audible)
        clipColour = emphasis::silenced (clipColour);

    g.setColour (clipColour.withAlpha (audible ? emphasis::subdued : emphasis::wash));
    g.fillRoundedRectangle (bounds, radius::sm);
    g.setColour (
        clipColour.brighter (emphasis::edgeLift).withAlpha (audible ? 1.0f : emphasis::dimmed));
    g.drawRoundedRectangle (bounds, radius::sm, stroke::regular);

    auto* pool = samplePool;
    const auto sample = channel.getChildWithName (ids::SAMPLE);
    const auto path = sample.isValid() ? sample[ids::file].toString() : juce::String();

    if (pool != nullptr && path.isNotEmpty())
    {
        const auto& entry = pool->loadReference (path);

        if (entry.isValid())
        {
            // Clipped to the clip, like the automation curve beside it: a
            // recording longer than the bars it was given must not draw over
            // the clip after it.
            const juce::Graphics::ScopedSaveState clipped (g);
            g.reduceClipRegion (bounds.toNearestInt());

            const auto trace = clipColour.brighter (emphasis::edgeLift)
                                   .withAlpha (audible ? emphasis::strong : emphasis::subdued);
            const juce::Range<float> span { bounds.getX(), bounds.getRight() };

            paint::waveform (g, { bounds.getY(), bounds.getBottom() }, span, span, entry.peaks,
                             [trace] (float) { return trace; });
        }
    }

    g.setColour (colour::textPrimary.withAlpha (audible ? emphasis::strong : emphasis::dimmed));
    g.setFont (type::font (type::small, true));
    // Ellipsised, like the automation label below and the pattern name further
    // down. A channel's name is whatever the user typed, and this was the one
    // of the three that hard-clipped it.
    g.drawText (channel.isValid() ? channel[ids::name].toString()
                                  : "channel " + clip[ids::channelId].toString(),
                bounds.reduced (6.0f, 2.0f).toNearestInt(), juce::Justification::topLeft, true);
}

void PlaylistComponent::paintAutomationClip (juce::Graphics& g, const juce::ValueTree& clip,
                                             int trackIndex, juce::Rectangle<float> bounds,
                                             bool audible)
{
    const auto automation = automationOf (clip);

    // What the clip DRIVES, resolved from the four properties it stores. Every
    // automation clip used to be amber whatever it automated, so a lane of them
    // said only "these are curves" - the one thing already obvious from their
    // shape.
    //
    // A pattern clip takes its channel's identity colour and an automation clip
    // takes its target's function colour, and the two are meant to read
    // differently: identity is loud, function is quiet. On a lane holding both
    // that difference is the whole point.
    const auto* spec = specForAutomation (document.getState(), automation);
    const auto targetColour = spec != nullptr ? palette::forRole (roleOf (*spec->property))
                                              : colour::warning;

    // An automation clip reads as a different kind of thing from a pattern
    // clip: no fill, a visible curve, and its own colour.
    const auto clipColour = audible ? targetColour : emphasis::silenced (targetColour);

    g.setColour (colour::wellDeep.withAlpha (emphasis::strong));
    g.fillRoundedRectangle (bounds, radius::sm);
    g.setColour (clipColour.withAlpha (audible ? emphasis::strong : emphasis::subdued));
    g.drawRoundedRectangle (bounds, radius::sm, stroke::regular);

    if (! automation.isValid())
    {
        g.setColour (colour::danger);
        g.setFont (type::font (type::caption));
        g.drawText (tr (StringId::playlist_missingAutomation),
                    bounds.toNearestInt().reduced (space::xs, 0), juce::Justification::centredLeft,
                    true);
        return;
    }

    // Underneath the curve rather than over it: the curve is the content, and
    // an automation lane is only a row tall.
    //
    // At `strong`, the same alpha the clip's own border carries, rather than at
    // `subdued`: a name is read, and subdued put it at 2.3:1 on the well behind
    // it. Being under the curve is what keeps it out of the way; being faint as
    // well only made it unreadable.
    g.setColour (clipColour.withAlpha (emphasis::strong));
    g.setFont (type::font (type::caption));
    g.drawText (automation[ids::name].toString(),
                bounds.toNearestInt().reduced (space::xs, space::xxs), juce::Justification::topLeft,
                true);

    const juce::Graphics::ScopedSaveState clipped (g);
    g.reduceClipRegion (bounds.toNearestInt());

    // The lane draws itself. Its painter samples the same evaluator the audio
    // thread reads, so a bend and a step are drawn as they are heard rather than
    // as the chord this used to draw.
    //
    // `bipolar` comes from the spec now. It was hard-coded false because the
    // resolution above did not exist, so a pan curve filled from the bottom
    // while the pan knob it drives filled from the centre - the curve and the
    // control disagreeing about where nothing is.
    automationLane::paintCurve (g, laneGeometry (clip, trackIndex),
                                ProjectEdits::sortedAutomationPoints (automation),
                                { clipColour, spec != nullptr && spec->bipolar,
                                  clip == hoveredSegmentClip ? hoveredSegment : -1 });
}

/** The lanes and the clips on them. Its own function because it is the only
    part of paint() that is CLIPPED: a lane can be scrolled now, and without a
    clip region the stripe and the divider of a half-scrolled first track are
    drawn straight over the ruler. Returns how many tracks it walked, which is
    what tells paint() whether to draw the empty state.
*/
int PlaylistComponent::paintLanes (juce::Graphics& g, int bottom, bool anySolo)
{
    int trackIndex = 0;

    const juce::Graphics::ScopedSaveState lanesClipped (g);
    g.reduceClipRegion (getLaneArea());

    for (const auto& track : playlist())
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        const auto y = (int) laneY (trackIndex);

        // A lane wholly outside the view paints nothing. Twenty tracks at the
        // tallest height would otherwise paint twenty lanes to show three.
        if (y >= viewBottom() || y + rows.height <= lanesTop())
        {
            ++trackIndex;
            continue;
        }

        const auto audible = ! (bool) track[ids::mute] && (! anySolo || (bool) track[ids::solo]);

        if (trackIndex % 2 == 1)
        {
            g.setColour (colour::wellDeep.withAlpha (emphasis::dimmed));
            g.fillRect (size::gutterTrack, y, getWidth() - size::gutterTrack, rows.height);
        }

        // The lane a clip is being dragged onto, so a cross-track drop lands
        // where you meant it to.
        if (gesture == Gesture::moving && trackIndex == dropTrackIndex)
        {
            g.setColour (colour::accent.withAlpha (emphasis::tint));
            g.fillRect (size::gutterTrack, y, getWidth() - size::gutterTrack, rows.height);
        }

        g.setColour (colour::divider);
        g.drawHorizontalLine (y, (float) size::gutterTrack, (float) getWidth());

        for (const auto& clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            const auto bounds = boundsForClip (clip, trackIndex).reduced (2.0f, 3.0f);

            if (! bounds.intersects (juce::Rectangle<float> ((float) size::gutterTrack,
                                                             (float) lanesTop(), contentWidth(),
                                                             (float) (bottom - lanesTop()))))
                continue;

            if (ProjectEdits::isAutomationClip (clip))
            {
                paintAutomationClip (g, clip, trackIndex, bounds, audible);
                continue;
            }

            if (ProjectEdits::isAudioClip (clip))
            {
                paintAudioClip (g, clip, bounds, audible);
                continue;
            }

            const auto pattern = ProjectEdits::findPattern (document.getState(),
                                                            (int) clip[ids::patternId]);

            const auto isCurrent = (int) clip[ids::patternId] == editorState.getCurrentPatternId();

            // The lane's colour if it has been given one, and the accent
            // otherwise - which is what every clip was before a lane could
            // carry a colour, so an untouched project is unchanged. Whether
            // this is the CURRENT pattern stays the saturation rather than the
            // hue, so choosing a colour costs nothing that was already being
            // said here.
            const auto base = entityColour::stored (trackAt (trackIndex)).value_or (colour::accent);

            auto clipColour = isCurrent ? base : emphasis::secondary (base);

            // A clip on a silenced track is drawn as silenced, so mute and solo
            // are visible in the arrangement and not only in the headers.
            if (! audible)
                clipColour = emphasis::silenced (clipColour);

            g.setColour (clipColour.withAlpha (audible ? emphasis::strong : emphasis::subdued));
            g.fillRoundedRectangle (bounds, radius::sm);
            g.setColour (clipColour.brighter (emphasis::edgeLift)
                             .withAlpha (audible ? 1.0f : emphasis::dimmed));
            g.drawRoundedRectangle (bounds, radius::sm, stroke::regular);

            g.setColour (colour::textOnAccent);
            g.setFont (type::font (type::small, true));
            g.drawText (pattern.isValid() ? pattern[ids::name].toString()
                                          : "pattern " + clip[ids::patternId].toString(),
                        bounds.toNearestInt().reduced (space::xs, 0),
                        juce::Justification::centredLeft, true);
        }

        ++trackIndex;
    }

    return trackIndex;
}

void PlaylistComponent::paint (juce::Graphics& g)
{
    const auto bars = numBars();
    const auto width = (float) timeline.pixelsPerStep;
    // Where the grid, the selection band and the playhead stop. lanesBottom
    // rather than tracksBottom, so a scrolled arrangement does not draw its
    // furniture over the strip below the view.
    const auto bottom = lanesBottom();
    // The grid is drawn over the WHOLE width, so bar lines and numbers reach
    // the edge of the window rather than stopping with the arrangement. The
    // same change the two pattern editors got - three views that all stopped
    // mid-panel would have become one that still did.
    const auto painted = timeline.visibleStepRange (contentWidth());
    const auto anySolo = [this]
    {
        for (const auto& track : playlist())
            if (track.hasType (ids::PLAYLIST_TRACK) && (bool) track[ids::solo])
                return true;

        return false;
    }();

    g.fillAll (colour::well);

    // Below the last track is not a lane that stopped working.
    paint::inertArea (g, { 0, bottom, getWidth(), juce::jmax (0, getHeight() - bottom) });

    // --- ruler ---------------------------------------------------------------
    g.setColour (colour::surface);
    g.fillRect (0, rulerTop(), getWidth(), size::rulerHeight);

    // The selection's strip goes down before the bar numbers, so the numbers
    // inside it stay legible instead of being washed out by it.
    if (editorState.hasBarSelection())
    {
        const auto selection = editorState.getSelectedBarRange();

        const auto fromX = (float) size::gutterTrack
                           + timeline.xForStep ((double) selection.getStart());
        const auto toX = (float) size::gutterTrack
                         + timeline.xForStep ((double) selection.getEnd());

        g.setColour (colour::accent.withAlpha (emphasis::dimmed));
        g.fillRect (juce::Rectangle<float> (fromX, (float) rulerTop(),
                                            juce::jmax (1.0f, toX - fromX),
                                            (float) size::rulerHeight)
                        .getIntersection ({ (float) size::gutterTrack, (float) rulerTop(),
                                            (float) getWidth() - (float) size::gutterTrack,
                                            (float) size::rulerHeight }));
    }

    // The same rung TimelineRuler numbers its bars at, and it has to stay the
    // same: the playlist paints its own ruler rather than hosting that one, so
    // these two lines are the whole of what keeps the two agreeing.
    g.setFont (type::font (type::small));

    for (int bar = painted.getStart(); bar < painted.getEnd(); ++bar)
    {
        const auto x = (float) size::gutterTrack + timeline.xForStep ((double) bar);

        if (x > (float) getWidth())
            break;

        const auto beyond = bar >= bars;

        g.setColour (beyond ? colour::textDisabled : colour::textSecondary);
        g.drawText (juce::String (bar + 1), (int) x + 3, rulerTop(), (int) width - 4,
                    size::rulerHeight, juce::Justification::centredLeft, false);

        g.setColour (beyond ? colour::dividerStrong.withAlpha (emphasis::subdued)
                            : colour::dividerStrong);
        g.drawVerticalLine ((int) x, (float) rulerTop(), (float) bottom);
    }

    // --- the selected span ----------------------------------------------------
    // Drawn over the ruler and down through the tracks, under everything else, so
    // it reads as a region of time rather than as another lane. The playhead gets
    // the same full-height treatment one layer up.
    if (editorState.hasBarSelection())
    {
        const auto selection = editorState.getSelectedBarRange();

        const auto fromX = (float) size::gutterTrack
                           + timeline.xForStep ((double) selection.getStart());
        const auto toX = (float) size::gutterTrack
                         + timeline.xForStep ((double) selection.getEnd());

        const juce::Rectangle<float> content ((float) size::gutterTrack, (float) rulerTop(),
                                              (float) getWidth() - (float) size::gutterTrack,
                                              (float) (bottom - rulerTop()));

        const juce::Rectangle<float> band (fromX, (float) rulerTop(),
                                           juce::jmax (1.0f, toX - fromX),
                                           (float) (bottom - rulerTop()));

        const auto visible = band.getIntersection (content);

        // Only a wash over the tracks; the ruler's solid strip was drawn earlier,
        // under the bar numbers. A wash alone is what this had first, and at an
        // alpha low enough not to bury the clips it was too faint to find -
        // useless for a marker whose whole job is saying what a render will
        // contain. The ruler is where the span can be stated outright without
        // covering anything up.
        g.setColour (colour::accent.withAlpha (emphasis::tint));
        g.fillRect (visible.withTrimmedTop ((float) size::rulerHeight)); // below the ruler strip

        g.setColour (colour::accent);

        for (const auto edge : { fromX, toX })
            if (edge >= (float) size::gutterTrack && edge <= (float) getWidth())
                g.fillRect (edge - stroke::regular * 0.5f, (float) rulerTop(), stroke::regular,
                            (float) (bottom - rulerTop()));
    }

    // --- tracks --------------------------------------------------------------
    const auto trackIndex = paintLanes (g, bottom, anySolo);

    // Past the end of the song.
    const auto endX = (float) size::gutterTrack + timeline.xForStep ((double) bars);

    if (endX < (float) getWidth())
        paint::beyondEnd (g,
                          { (int) endX, lanesTop(), getWidth() - (int) endX,
                            juce::jmax (0, bottom - lanesTop()) },
                          endX);

    g.setColour (colour::dividerStrong);
    g.drawVerticalLine (size::gutterTrack, (float) rulerTop(), (float) bottom);
    g.drawHorizontalLine (lanesTop() - 1, 0.0f, (float) getWidth());

    // --- playhead ------------------------------------------------------------
    if (engine.getMode() == Transport::Mode::song)
    {
        const auto x = playheadX();

        if (x >= (float) size::gutterTrack)
        {
            playhead.set (engine.isPlaying());

            timelinePaint::playheadLine (g, x, { (float) lanesTop(), (float) bottom },
                                         playhead.brightness());

            // A head on the ruler, so the position is findable at a glance.
            timelinePaint::playheadHead (g, x, (float) lanesTop(), playhead.brightness());
        }
    }

    if (trackIndex == 0)
    {
        paint::emptyState (g, getLocalBounds(), "This project has no playlist tracks");
    }
    paint::cursorOutline (g, boundsForCell (cursor.getPosition().x, cursor.getPosition().y),
                          cursor.isPlaced());
}

} // namespace dew
