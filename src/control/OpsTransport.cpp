#include "control/OpsSupport.h"
#include "engine/AudioEngine.h"

namespace dew::control
{

namespace
{

ControlResult write (ControlHost& host, const juce::var& args)
{
    auto* engine = host.engine();

    if (engine == nullptr)
        return ControlResult::failure ("this build has no transport.");

    const auto project = host.project();

    // The engine reads the document through a snapshot published on the message
    // thread. Pressing play before a pending rebuild has been applied would
    // play the arrangement as it was before the notes this same client just
    // wrote, which is the sort of disagreement that looks like a lost edit.
    host.flushEngine();

    if (hasArg (args, "mode"))
        engine->setMode (textArg (args, "mode") == "pattern" ? Transport::Mode::pattern
                                                             : Transport::Mode::song);

    if (hasArg (args, "playheadSteps"))
        engine->setPlayheadSteps (numberArg (args, "playheadSteps"));

    // Both, and in this order: setStartMarkerSteps moves the playhead too, so a
    // call carrying only a marker leaves the two agreeing the way a click on a
    // ruler does.
    if (hasArg (args, "startMarkerSteps"))
        engine->setStartMarkerSteps (numberArg (args, "startMarkerSteps"));

    if (flagArg (args, "rewind"))
        engine->rewind();

    if (hasArg (args, "playing"))
    {
        if (flagArg (args, "playing"))
            engine->play();
        else
            engine->pause();
    }

    return ControlResult::success (
        Obj {}
            .set ("playing", engine->isPlaying())
            .set ("mode", engine->getMode() == Transport::Mode::pattern ? "pattern" : "song")
            .set ("startMarkerSteps", engine->getStartMarkerSteps())
            .set (ids::tempoBpm, project.isValid() ? (double) project[ids::tempoBpm] : 0.0));
}

ControlResult read (ControlHost& host, const juce::var&)
{
    auto* engine = host.engine();

    if (engine == nullptr)
        return ControlResult::failure ("this build has no transport.");

    return ControlResult::success (
        Obj {}
            .set ("playing", engine->isPlaying())
            .set ("mode", engine->getMode() == Transport::Mode::pattern ? "pattern" : "song")
            .set ("playheadSteps", engine->getPlayheadSteps())
            .set ("startMarkerSteps", engine->getStartMarkerSteps()));
}

} // namespace

void appendTransportOps (std::vector<OpSpec>& all)
{
    all.push_back (
        { "transport_write",
          OpScope::write,
          OpEdits::no,
          "Start or pause playback, choose song or pattern mode, and move the playhead "
          "or the start marker.",
          "Not an edit: nothing here touches the document or the undo history. It is "
          "what the transport bar does.\n\n"
          "Any pending change to the document is applied to the engine first, so playing "
          "immediately after writing notes plays the notes you just wrote.",
          { { "playing", ValueKind::flag, false,
              "True plays, false pauses - which returns to the start marker." },
            { "mode", ValueKind::text, false,
              "song plays the arrangement, pattern loops the current pattern." },
            { "playheadSteps", ValueKind::number, false, "Move the playhead, in steps." },
            { "startMarkerSteps", ValueKind::number, false,
              "Where playback begins and where pausing returns to, in steps. Moves the "
              "playhead with it." },
            { "rewind", ValueKind::flag, false,
              "Return to the start: the playhead and the marker both." } },
          write });

    all.push_back ({ "transport_read",
                     OpScope::read,
                     OpEdits::no,
                     "Report whether dew is playing, in which mode, and where it is.",
                     "A read, so it is available to a read-only grant. Use it to tell whether "
                     "something you started is still going.",
                     {},
                     read });
}

} // namespace dew::control
