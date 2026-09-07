#include "ui/StatusBar.h"

#include "i18n/Strings.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/ButtonBehaviour.h"

namespace dew
{

using namespace tokens;

StatusBar::StatusBar (ProjectDocument& d, EditorState& s, LiveAudioHost& h)
    : document (d)
    , editorState (s)
    , audioHost (h)
{
    setComponentID ("statusBar");

    document.getState().addListener (this);
    editorState.addChangeListener (this);

    updateContext();
    startTimerHz (motion::uiRefreshHz);
}

StatusBar::~StatusBar()
{
    editorState.removeChangeListener (this);
    document.getState().removeListener (this);
}

void StatusBar::refresh()
{
    document.getState().addListener (this);
    updateContext();
    repaint();
}

bool StatusBar::hasMessage() const
{
    return messageText.isNotEmpty() && messageAgeMs < messageLifetimeMs;
}

void StatusBar::showMessage (const juce::String& text, Severity severity)
{
    // A standing message of higher severity is not displaced by a lesser one:
    // an error must not be buried by a routine "saved" a moment later.
    if (hasMessage() && severity < messageSeverity)
        return;

    messageText = text;
    messageSeverity = severity;
    messageAgeMs = 0;

    // Fades IN as well as out. The fade-out was already here; a message that
    // appeared as a hard cut and left as a fade read as two different things
    // happening.
    arrival.snapTo (0.0f);
    arrival.animateTo (1.0f, tokens::motion::popupMs, Ease::decelerate);

    // Said out loud as well as drawn. This strip is where dew reports that the
    // audio device would not open or that a project failed to load, and a
    // message that only ever appears in a corner is a message a screen reader
    // user never gets - it is not attached to anything they are focused on.
    //
    // The severity rule above already says an error and a routine "saved" are
    // not comparable, so it decides the priority here too rather than a second
    // idea of importance. Static, and compiled to nothing where the platform
    // has no accessibility backend.
    juce::AccessibilityHandler::postAnnouncement (
        text, severity == Severity::error
                  ? juce::AccessibilityHandler::AnnouncementPriority::high
                  : juce::AccessibilityHandler::AnnouncementPriority::medium);

    repaint (messageBounds);
}

void StatusBar::advanceMessageClock (int milliseconds)
{
    const auto had = hasMessage();
    messageAgeMs += milliseconds;

    if (had != hasMessage())
        repaint (messageBounds);
}

void StatusBar::updateContext()
{
    const auto channel = ProjectEdits::findChannel (document.getState(),
                                                    editorState.getSelectedChannelId());
    const auto pattern = ProjectEdits::findPattern (document.getState(),
                                                    editorState.getCurrentPatternId());

    juce::StringArray parts;

    if (channel.isValid())
        parts.add (channel[ids::name].toString());

    if (pattern.isValid())
    {
        parts.add (pattern[ids::name].toString());
        parts.add (
            tr (StringId::status_patternSteps, Args {}.count ((int) pattern[ids::lengthSteps])));

        int notes = 0;

        for (const auto& note : pattern)
            if (note.hasType (ids::NOTE) && channel.isValid()
                && (int) note[ids::ch] == (int) channel[ids::id])
                ++notes;

        parts.add (tr (StringId::status_notes, Args {}.count (notes)));
    }

    const auto wanted = parts.joinIntoString (tr (StringId::shared_fieldSeparator));

    if (wanted != contextText)
    {
        contextText = wanted;
        repaint (contextBounds);
    }
}

void StatusBar::timerCallback()
{
    // Also polled, not only driven by change messages: EditorState broadcasts
    // asynchronously, and a status line that is occasionally a beat stale is
    // worse than one that costs a few string comparisons per tick. It only
    // repaints when the text actually differs.
    updateContext();

    advanceMessageClock (1000 / juce::jmax (1, motion::uiRefreshHz));

    auto& manager = audioHost.getDeviceManager();

    const auto load = manager.getCpuUsage();
    const auto drops = manager.getXRunCount();

    if (drops > lastDropouts)
    {
        // Flash rather than only counting: a dropout you have to notice a
        // number changing to learn about is one you will not notice.
        dropoutFlashMs = 900;
        lastDropouts = drops;
    }
    else if (dropoutFlashMs > 0)
    {
        dropoutFlashMs = juce::jmax (0,
                                     dropoutFlashMs - 1000 / juce::jmax (1, motion::uiRefreshHz));
    }

    // getXRunCount returns -1 when the device cannot report them at all, which
    // is not the same as "no dropouts" and must not be shown as zero.
    if (! juce::approximatelyEqual (load, dspLoad) || drops != dropouts || dropoutFlashMs > 0)
    {
        dspLoad = load;
        dropouts = drops;
        repaint (loadBounds);
    }
}

void StatusBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateContext();
}

void StatusBar::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    updateContext();
}
void StatusBar::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)
{
    updateContext();
}
void StatusBar::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    updateContext();
}

juce::Colour StatusBar::colourFor (Severity severity) const
{
    switch (severity)
    {
        case Severity::success: return colour::success;
        case Severity::warning: return colour::warning;
        case Severity::error: return colour::danger;
        case Severity::info: break;
    }

    return colour::textSecondary;
}

void StatusBar::setHoverHelp (const juce::String& text)
{
    if (hoverText == text)
        return;

    hoverText = text;
    repaint();
}

void StatusBar::resized()
{
    auto area = getLocalBounds().reduced (space::md, 0);

    loadBounds = area.removeFromRight (150);
    area.removeFromRight (space::lg);
    contextBounds = area.removeFromLeft (juce::jmax (0, area.getWidth() / 2));
    area.removeFromLeft (space::lg);
    messageBounds = area;
}

void StatusBar::paint (juce::Graphics& g)
{
    g.fillAll (colour::surface);

    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    g.setFont (type::font (type::small));

    g.setColour (colour::textSecondary);
    g.drawText (contextText, contextBounds, juce::Justification::centredLeft, true);

    if (hasMessage())
    {
        // Fades out over its last second rather than disappearing, so a message
        // that has gone does not look like one you missed.
        const auto remaining = messageLifetimeMs - messageAgeMs;
        const auto leaving = juce::jlimit (0.0f, 1.0f, (float) remaining / 1000.0f);

        g.setColour (colourFor (messageSeverity).withAlpha (juce::jmin (arrival.get(), leaving)));
        g.drawText (messageText, messageBounds, juce::Justification::centredLeft, true);
    }
    else if (hoverText.isNotEmpty())
    {
        // Quieter than a message and quieter than the context, because it is
        // the one line here that changes as the pointer moves: at message
        // weight it would flicker across the bottom of the window all day.
        g.setColour (colour::textDisabled);
        g.drawText (hoverText, messageBounds, juce::Justification::centredLeft, true);
    }

    // --- load and dropouts ---------------------------------------------------
    const auto percent = juce::roundToInt (dspLoad * 100.0);

    auto right = tr (StringId::status_dspLoad, Args {}.with ("percent", percent));

    if (dropouts >= 0)
    {
        right << tr (StringId::shared_fieldSeparator);
        right << tr (StringId::status_dropouts, Args {}.count (dropouts));
    }

    g.setColour (dropoutFlashMs > 0 ? colour::danger
                 : percent > 80     ? colour::warning
                                    : colour::textDisabled);
    g.drawText (right, loadBounds, juce::Justification::centredRight, false);
}

} // namespace dew
