#include "io/MidiExporter.h"

#include "i18n/Strings.h"
#include "model/Ids.h"

namespace dew
{

namespace
{

/** One note, already placed on the song's step grid. */
struct PlacedNote
{
    int channelIndex = 0;
    juce::int64 step = 0;
    int lengthSteps = 1;
    int pitch = 60;
    float velocity = 1.0f;

    bool operator< (const PlacedNote& other) const noexcept
    {
        if (step != other.step)
            return step < other.step;
        if (channelIndex != other.channelIndex)
            return channelIndex < other.channelIndex;
        return pitch < other.pitch;
    }

    /** Two notes the same instrument starts at the same moment on the same
        pitch. In audio that is two voices; in MIDI it is an overlapping pair
        whose note-off is ambiguous.
    */
    bool sameEventAs (const PlacedNote& other) const noexcept
    {
        return step == other.step && channelIndex == other.channelIndex && pitch == other.pitch;
    }
};

/** Every note the sequencer would play over the whole material.

    Mirrors Sequencer::collect rather than reinterpreting it: same clip bounds,
    same repeat modulo, same playlist-track audibility. If that function changes,
    this has to change with it, and MidiExportTests fails until it does.
*/
std::vector<PlacedNote> collectNotes (const EngineSnapshot& snapshot,
                                      const MidiExportOptions& options, int patternIndex,
                                      juce::StringArray& warnings)
{
    std::vector<PlacedNote> notes;

    const auto push = [&] (const NoteSnapshot& note, juce::int64 step)
    {
        if (note.channelIndex < 0 || note.channelIndex >= (int) snapshot.channels.size())
            return;

        const auto& channel = snapshot.channels[(size_t) note.channelIndex];

        if (! options.includeMutedChannels && ! snapshot.isChannelAudible (channel))
            return;

        // note.pitch is absolute. The channel's basePitch is where the piano roll
        // centres and what a lit step-grid cell writes; the engine never reads it,
        // so adding it here would transpose the MIDI away from the audio.
        notes.push_back ({ note.channelIndex, step, juce::jmax (1, note.lengthSteps), note.pitch,
                           note.velocity });
    };

    if (options.mode == Transport::Mode::pattern)
    {
        if (patternIndex < 0 || patternIndex >= (int) snapshot.patterns.size())
            return notes;

        for (const auto& note : snapshot.patterns[(size_t) patternIndex].notes)
            push (note, note.step);

        return notes;
    }

    const auto stepsPerBar = snapshot.stepsPerBar();

    for (const auto& clip : snapshot.clips)
    {
        if (clip.patternIndex < 0 || clip.patternIndex >= (int) snapshot.patterns.size())
            continue;

        // A muted, or un-soloed, playlist track schedules nothing.
        if (! clip.trackAudible)
            continue;

        const auto& pattern = snapshot.patterns[(size_t) clip.patternIndex];

        if (pattern.lengthSteps <= 0)
            continue;

        const auto clipStart = (juce::int64) clip.startBar * stepsPerBar;
        const auto clipEnd = clipStart + (juce::int64) clip.lengthBars * stepsPerBar;

        for (auto step = clipStart; step < clipEnd; ++step)
        {
            // A clip longer than its pattern repeats it, as FL does.
            const auto local = (int) ((step - clipStart) % (juce::int64) pattern.lengthSteps);

            for (const auto& note : pattern.notes)
                if (note.step == local)
                    push (note, step);
        }
    }

    std::sort (notes.begin(), notes.end());

    // Two playlist tracks holding the same pattern at the same bar produce the
    // same note twice. Audio sums two voices; MIDI would get an overlapping
    // note-on/note-off pair and an importer's guess about which off matches which on.
    const auto duplicate = std::unique (notes.begin(), notes.end(),
                                        [] (const PlacedNote& a, const PlacedNote& b)
                                        { return a.sameEventAs (b); });

    if (duplicate != notes.end())
    {
        warnings.add (tr (StringId::warning_midiDuplicateNotes));
        notes.erase (duplicate, notes.end());
    }

    return notes;
}

/** Names, which the snapshot deliberately does not carry - a juce::String has no
    business in a struct the audio thread reads.
*/
juce::StringArray channelNames (const juce::ValueTree& project)
{
    juce::StringArray names;

    for (const auto& child : project)
        if (child.hasType (ids::CHANNEL))
            names.add (child[ids::name].toString());

    return names;
}

} // namespace

int MidiExporter::ticksPerQuarterNoteFor (int stepsPerBeat) noexcept
{
    const auto steps = juce::jmax (1, stepsPerBeat);
    const auto ticksPerStep = juce::jmax (1, juce::roundToInt (960.0 / (double) steps));

    return ticksPerStep * steps;
}

int MidiExporter::midiChannelFor (int channelIndex) noexcept
{
    static constexpr int usable[] { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16 };
    static constexpr int numUsable = (int) (sizeof (usable) / sizeof (usable[0]));

    return usable[juce::jmax (0, channelIndex) % numUsable];
}

juce::MidiFile MidiExporter::build (const juce::ValueTree& project,
                                    const MidiExportOptions& options, juce::StringArray& warnings,
                                    juce::int64& numNotes)
{
    numNotes = 0;

    const auto snapshot = buildSnapshot (project, &warnings);
    const auto patternIndex = snapshot.patternIndexForId (options.patternId);
    const auto names = channelNames (project);

    const auto stepsPerBar = snapshot.stepsPerBar();
    const auto ticksPerQuarter = ticksPerQuarterNoteFor (snapshot.stepsPerBeat);
    const auto ticksPerStep = (double) ticksPerQuarter
                              / (double) juce::jmax (1, snapshot.stepsPerBeat);

    auto notes = collectNotes (snapshot, options, patternIndex, warnings);

    // A range trims and rebases; without one, everything from step zero.
    juce::int64 firstStep = 0;
    auto lastStep = std::numeric_limits<juce::int64>::max();

    if (! options.barRange.isEmpty())
    {
        firstStep = (juce::int64) options.barRange.firstBar * stepsPerBar;
        lastStep = (juce::int64) options.barRange.lastBar * stepsPerBar;

        bool anyHeldOver = false;

        for (const auto& note : notes)
            anyHeldOver = anyHeldOver
                          || (note.step < firstStep && note.step + note.lengthSteps > firstStep);

        if (anyHeldOver)
            warnings.add (tr (StringId::warning_midiHeldOverNotes));
    }

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerQuarter);

    juce::int64 lastTick = 0;

    // --- the notes, one sequence per channel ---------------------------------
    std::map<int, juce::MidiMessageSequence> sequences;

    for (const auto& note : notes)
    {
        auto step = note.step;
        auto length = (juce::int64) note.lengthSteps;

        if (! options.barRange.isEmpty())
        {
            if (note.step + length <= firstStep || note.step >= lastStep)
                continue;

            // A note that starts before the range is retriggered at its start.
            if (step < firstStep)
            {
                length -= (firstStep - step);
                step = firstStep;
            }

            step -= firstStep;
        }

        const auto onTick = (juce::int64) std::llround ((double) step * ticksPerStep);

        // One tick short, so two consecutive notes on the same pitch do not touch
        // - which many importers render as one long note, or as a stuck one.
        const auto offTick = juce::jmax (
            onTick + 1, (juce::int64) std::llround ((double) (step + length) * ticksPerStep) - 1);

        const auto midiChannel = midiChannelFor (note.channelIndex);

        // Velocity 0 IS a note-off, so a quiet note must not become one.
        const auto velocity = (juce::uint8) juce::jlimit (
            1, 127, juce::roundToInt (note.velocity * 127.0f));

        auto& sequence = sequences[note.channelIndex];

        sequence.addEvent (juce::MidiMessage::noteOn (midiChannel, note.pitch, velocity),
                           (double) onTick);
        sequence.addEvent (juce::MidiMessage::noteOff (midiChannel, note.pitch, (juce::uint8) 0),
                           (double) offTick);

        lastTick = juce::jmax (lastTick, offTick);
        ++numNotes;
    }

    // --- the conductor track --------------------------------------------------
    juce::MidiMessageSequence conductor;

    const auto projectName = project[ids::name].toString();

    conductor.addEvent (juce::MidiMessage::textMetaEvent (
                            3, projectName.isNotEmpty() ? projectName : juce::String ("dew")),
                        0.0);

    // ONE event per run of equal tempo, from the same map the audio path uses,
    // so a MIDI export of a tempo curve lands where the WAV of it does.
    //
    // The note ticks do not move at all, and that is the point: a tick is
    // musical time, and the tempo map is precisely the tick-to-seconds function
    // a tempo meta event expresses. A ramp therefore exports without shifting a
    // single note.
    //
    // Microseconds per quarter note, not BPM.
    const auto tempoEvent = [] (double bpm)
    {
        return juce::MidiMessage::tempoMetaEvent (
            juce::roundToInt (60'000'000.0 / juce::jmax (1.0, bpm)));
    };

    const auto segments = snapshot.tempoMap->segments();

    // The PREVAILING tempo at tick 0, whatever it is. A range that begins
    // mid-ramp would otherwise start at the project's default and catch up at
    // the next entry, which is a tempo nobody wrote.
    auto prevailing = snapshot.tempoBpm;

    for (const auto& segment : segments)
        if (segment.startStep <= (double) firstStep)
            prevailing = segment.bpm;

    conductor.addEvent (tempoEvent (prevailing), 0.0);

    for (const auto& segment : segments)
    {
        if (segment.startStep <= (double) firstStep || segment.startStep >= (double) lastStep)
            continue;

        conductor.addEvent (tempoEvent (segment.bpm),
                            (double) std::llround ((segment.startStep - (double) firstStep)
                                                   * (double) ticksPerStep));
    }

    // The denominator is NOTATIONAL. dew's beat is a quarter note's worth of
    // ticks whatever beatUnit says - ticksPerQuarterNoteFor and the tempo event
    // above both depend on stepsPerBeat alone - so a 6/8 project exports its
    // notes at the right ticks under a 6/8 signature, and an importing DAW,
    // which reads that bar as three quarter notes where dew's is six, draws its
    // bar lines somewhere else. Rescaling the ticks instead would move every
    // note to keep the barring, which is the worse of the two trades: the notes
    // are the content and the barring is the label.
    //
    // juce::MidiMessage stores log2 of the denominator and rounds UP to the
    // next power of two, which is why beatUnit is constrained to one already.
    conductor.addEvent (
        juce::MidiMessage::timeSignatureMetaEvent (snapshot.beatsPerBar, snapshot.beatUnit), 0.0);

    if (sequences.size() > 15)
        warnings.add (tr (StringId::warning_midiChannelsShared));

    if (options.oneTrackPerChannel)
    {
        conductor.addEvent (juce::MidiMessage::endOfTrack(), (double) lastTick);
        file.addTrack (conductor);

        for (auto& [channelIndex, sequence] : sequences)
        {
            sequence.updateMatchedPairs();

            const auto name = juce::isPositiveAndBelow (channelIndex, names.size())
                                  ? names[channelIndex]
                                  : "Channel " + juce::String (channelIndex + 1);

            juce::MidiMessageSequence named;
            named.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);
            named.addSequence (sequence, 0.0, 0.0, (double) lastTick + 1.0);
            named.addEvent (juce::MidiMessage::endOfTrack(), (double) lastTick);

            file.addTrack (named);
        }

        return file;
    }

    // Format 0 is exactly one track, or the header count is a lie.
    for (auto& [channelIndex, sequence] : sequences)
    {
        juce::ignoreUnused (channelIndex);
        conductor.addSequence (sequence, 0.0, 0.0, (double) lastTick + 1.0);
    }

    conductor.updateMatchedPairs();
    conductor.addEvent (juce::MidiMessage::endOfTrack(), (double) lastTick);
    file.addTrack (conductor);

    return file;
}

RenderReport MidiExporter::writeToFile (const juce::ValueTree& project,
                                        const juce::File& destination,
                                        const MidiExportOptions& options)
{
    RenderReport report;

    juce::int64 numNotes = 0;
    const auto file = build (project, options, report.warnings, numNotes);

    if (numNotes == 0)
    {
        report.result = juce::Result::fail ("There are no notes in what you asked to export.");
        return report;
    }

    report.numSamples = numNotes;

    destination.getParentDirectory().createDirectory();

    juce::TemporaryFile temp (destination);

    {
        juce::FileOutputStream stream (temp.getFile());

        if (! stream.openedOk())
        {
            report.result = juce::Result::fail ("Could not create "
                                                + destination.getFullPathName());
            return report;
        }

        if (! file.writeTo (stream, options.oneTrackPerChannel ? 1 : 0))
        {
            report.result = juce::Result::fail ("Could not write MIDI to "
                                                + destination.getFullPathName());
            return report;
        }

        stream.flush();
    }

    if (! temp.overwriteTargetFileWithTemporary())
    {
        report.result = juce::Result::fail ("Could not replace " + destination.getFullPathName());
        return report;
    }

    report.files.add (destination);
    return report;
}

} // namespace dew
