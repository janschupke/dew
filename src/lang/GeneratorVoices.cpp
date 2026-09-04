// =============================================================================
// The eight ways a part turns into notes.
//
// One of two translation units defining lang/GeneratorImpl.h.
//
// Each is a leaf: it reads the chord spans, the channel and the part it was
// given, draws from the DrawSite it was handed, and appends to a note vector.
// None of them decides where it sits in the song - renderPart does that, in
// Generator.cpp - which is what let them move here together.
//
// Two are pure statics with no `this` at all: transposePitch, which walks a
// pitch to where the harmony has moved, and degreeForSource.
// =============================================================================

#include "lang/Generator.h"

#include "lang/GeneratorImpl.h"

#include <algorithm>
#include <cmath>

#include "lang/Counterpoint.h"
#include "lang/Grid.h"
#include "lang/Harmony.h"
#include "lang/Melody.h"
#include "lang/Voicing.h"

namespace dew::lang
{

void Generator::renderChords (const PartSpec& part, const ChannelSpec& channel,
                              const std::vector<ChordSpan>& spans, int track, int transpose,
                              std::vector<Note>& notes, const DrawSite& site, int stepsPerBar)
{
    const auto* voicingSpec = model.voicing (part.voicing);

    if (voicingSpec == nullptr)
        return;

    auto spec = *voicingSpec;

    // A part's own range narrows the voicing's register, so one voicing can
    // serve two channels sitting in different places.
    spec.lowPitch = std::max (spec.lowPitch, channel.lowPitch);
    spec.highPitch = std::min (spec.highPitch, channel.highPitch);

    if (spec.highPitch < spec.lowPitch)
    {
        spec.lowPitch = channel.lowPitch;
        spec.highPitch = channel.highPitch;
    }

    std::vector<int> previous;
    auto ordinal = 0;

    const auto* rhythm = rhythmFor (part);

    for (const auto& span : spans)
    {
        const auto pitches = voiceChord (span.chord, spec, previous);
        previous = pitches;

        // With no rhythm the chord is one held sound; with one, it is struck
        // on each onset of the rhythm within the span.
        std::vector<Onset> onsets;

        if (rhythm != nullptr)
        {
            RhythmSpec local = *rhythm;
            onsets = tileRhythm (local, span.endStep - span.startStep, stepsPerBarOf(),
                                 score.beatUnit, score.stepsPerBeat);
        }

        if (onsets.empty())
            onsets.push_back (
                { 0, span.endStep - span.startStep, false, false, MetricStrength::barStart });

        for (const auto& onset : onsets)
        {
            if (onset.isRest)
                continue;

            for (const auto pitch : pitches)
            {
                const auto sounded = pitch + transpose;

                if (sounded < lowestPitch || sounded > highestPitch)
                    continue;

                notes.push_back ({ track, span.startStep + onset.startStep, onset.lengthSteps,
                                   sounded,
                                   velocityFor (channel, site, ordinal,
                                                (span.startStep + onset.startStep)
                                                    / std::max (1, stepsPerBar)) });
            }

            ++ordinal;
        }
    }
}

void Generator::renderLine (const PartSpec& part, const ChannelSpec& channel,
                            const std::vector<ChordSpan>& spans, int track, int transpose,
                            int totalSteps, int stepsPerBar, std::vector<Note>& notes,
                            const DrawSite& site)
{
    const auto* rhythm = rhythmFor (part);

    std::vector<Onset> onsets;

    if (rhythm != nullptr)
        onsets = tileRhythm (*rhythm, totalSteps, stepsPerBar, score.beatUnit, score.stepsPerBeat);

    if (onsets.empty())
        for (const auto& span : spans)
            onsets.push_back ({ span.startStep, span.endStep - span.startStep, false, false,
                                MetricStrength::barStart });

    auto ordinal = 0;

    for (const auto& onset : onsets)
    {
        if (onset.isRest)
            continue;

        const ChordSpan* span = nullptr;

        for (const auto& candidate : spans)
            if (onset.startStep >= candidate.startStep && onset.startStep < candidate.endStep)
                span = &candidate;

        if (span == nullptr)
            continue;

        const auto degree = degreeForSource (part.lineSource, ordinal,
                                             span->chord.intervals.size());
        const auto interval = span->chord.intervals[(std::size_t) degree];

        // Placed in the channel's own register rather than transposed
        // blindly, so a bass line lands where the bass lives.
        auto pitch = pitchClassOf (span->chord.rootPc + interval);

        while (pitch < channel.lowPitch)
            pitch += 12;

        while (pitch > channel.highPitch && pitch - 12 >= channel.lowPitch)
            pitch -= 12;

        const auto sounded = pitch + transpose;

        if (sounded >= lowestPitch && sounded <= highestPitch)
            notes.push_back ({ track, onset.startStep, onset.lengthSteps, sounded,
                               velocityFor (channel, site, ordinal,
                                            onset.startStep / std::max (1, stepsPerBar)) });

        ++ordinal;
    }
}

int Generator::degreeForSource (LineSource source, int ordinal, std::size_t available)
{
    const auto count = (int) available;

    if (count <= 0)
        return 0;

    if (source == LineSource::root)
        return 0;

    if (source == LineSource::rootFifth)
    {
        // Root then fifth, alternating - the second interval if there is one.
        const auto fifth = count > 2 ? 2 : 0;
        return ordinal % 2 == 0 ? 0 : fifth;
    }

    // Root, third, fifth in turn.
    const int degrees[] = { 0, count > 1 ? 1 : 0, count > 2 ? 2 : 0 };
    return degrees[ordinal % 3];
}

void Generator::renderMelody (const PartSpec& part, const ChannelSpec& channel,
                              const std::vector<ChordSpan>& spans, int track, int transpose,
                              int totalSteps, int stepsPerBar, std::vector<Note>& notes,
                              const DrawSite& site)
{
    const auto* rhythm = rhythmFor (part);

    if (rhythm == nullptr)
        return;

    auto onsets = tileRhythm (*rhythm, totalSteps, stepsPerBar, score.beatUnit, score.stepsPerBeat,
                              part.melody.align == Alignment::bar);

    if (onsets.empty())
        return;

    if (part.melody.muteCount > 0)
        applyMuteBudget (onsets, part.melody.muteCount, part.melody.muteWindow,
                         site.instance.child ("mute"), stepsPerBar);

    const auto low = part.melody.hasRange ? part.melody.lowPitch : channel.lowPitch;
    const auto high = part.melody.hasRange ? part.melody.highPitch : channel.highPitch;

    // The cadence is drawn ONCE, here, at whatever scope it declares - the
    // melody generator is handed a degree, not a choice, so it needs to
    // know nothing about scopes or seeds.
    const auto cadence = chooseDegree (part.melody.cadence, site);

    const auto line = generateMelody (onsets, spans, part.melody, low, high,
                                      site.instance.child ("melody"), cadence);

    auto ordinal = 0;

    for (const auto& note : line)
    {
        const auto sounded = note.pitch + transpose;

        if (sounded >= lowestPitch && sounded <= highestPitch)
            notes.push_back ({ track, note.startStep, note.lengthSteps, sounded,
                               velocityFor (channel, site, ordinal,
                                            note.startStep / std::max (1, stepsPerBar)) });

        ++ordinal;
    }
}

std::vector<int> Generator::voiceAt (const std::vector<Note>& notes, int track,
                                     const std::vector<Onset>& onsets) const
{
    std::vector<int> sounding (onsets.size(), -1);

    for (std::size_t i = 0; i < onsets.size(); ++i)
    {
        const auto step = onsets[i].startStep;
        auto lowest = -1;

        for (const auto& note : notes)
            if (note.track == track && note.startStep <= step
                && step < note.startStep + note.lengthSteps)
                lowest = lowest < 0 ? note.pitch : std::min (lowest, note.pitch);

        sounding[i] = lowest;
    }

    return sounding;
}

void Generator::renderImitation (const PartSpec& part, const ChannelSpec& channel,
                                 const std::vector<ChordSpan>& spans, int track, int transpose,
                                 int totalSteps, int stepsPerBar, std::vector<Note>& notes,
                                 const DrawSite& site)
{
    const auto source = trackIndexFor (part.imitation.source);

    if (source < 0)
        return;

    // A COPY, taken before anything is appended: writing into `notes` while
    // reading it would let the imitation imitate itself, one delay at a
    // time, until the section filled up.
    std::vector<Note> copied;

    for (const auto& note : notes)
        if (note.track == source)
            copied.push_back (note);

    if (copied.empty())
    {
        auto& d = diagnostics.warning (
            "W605",
            diagnostics.text (Msg::generator_nothingToImitate_message,
                              MsgArgs {}.with ("channel", part.imitation.source)),
            part.imitation.sourceRange);
        d.notes.push_back (diagnostics.text (Msg::generator_nothingToImitate_note));
        return;
    }

    const auto delay = part.imitation.delaySteps * stepsPerBar;
    auto ordinal = 0;

    for (const auto& note : copied)
    {
        const auto start = note.startStep + delay;

        // Anything past the section's end is DROPPED rather than wrapped: a
        // canon that wrapped would answer itself from the future.
        if (start >= totalSteps)
            continue;

        const auto shifted = transposePitch (note.pitch, spans, start, part.imitation);
        const auto sounded = shifted + transpose;

        if (sounded < channel.lowPitch || sounded > channel.highPitch)
            continue;

        if (sounded < lowestPitch || sounded > highestPitch)
            continue;

        notes.push_back (
            { track, start, std::min (note.lengthSteps, totalSteps - start), sounded,
              velocityFor (channel, site, ordinal, start / std::max (1, stepsPerBar)) });
        ++ordinal;
    }
}

int Generator::transposePitch (int pitch, const std::vector<ChordSpan>& spans, int step,
                               const ImitationSpec& spec)
{
    if (spec.transpose == 0)
        return pitch;

    if (spec.mode == TransposeMode::chromatic)
        return pitch + spec.transpose;

    const ChordSpan* span = nullptr;

    for (const auto& candidate : spans)
        if (step >= candidate.startStep && step < candidate.endStep)
            span = &candidate;

    if (span == nullptr)
        return pitch + spec.transpose;

    // Walk the local scale by degrees. Wider than the scale is fine: the
    // walk simply keeps going into the next octave.
    const auto scale = scalePitchesBetween (span->localKey, 0, 127);

    if (scale.empty())
        return pitch + spec.transpose;

    // The nearest scale tone at or below the note, so a chromatic passing
    // note moves with its neighbours rather than being left behind.
    auto index = 0;

    for (std::size_t i = 0; i < scale.size(); ++i)
        if (scale[i] <= pitch)
            index = (int) i;

    const auto wanted = std::clamp (index + spec.transpose, 0, (int) scale.size() - 1);

    return scale[(std::size_t) wanted] + (pitch - scale[(std::size_t) index]);
}

void Generator::renderCounterpoint (const PartSpec& part, const ChannelSpec& channel,
                                    const std::vector<ChordSpan>& spans, int track, int transpose,
                                    int totalSteps, int stepsPerBar, std::vector<Note>& notes,
                                    const DrawSite& site)
{
    const auto* rhythm = rhythmFor (part);

    if (rhythm == nullptr)
        return;

    const auto against = trackIndexFor (part.counterpoint.against);

    if (against < 0)
        return;

    const auto onsets = tileRhythm (*rhythm, totalSteps, stepsPerBar, score.beatUnit,
                                    score.stepsPerBeat, part.counterpoint.align == Alignment::bar);

    if (onsets.empty())
        return;

    // The voice it answers has to have been written already. Parts render
    // in the order they are declared, so this is a real constraint and
    // saying so is better than answering silence.
    const auto other = voiceAt (notes, against, onsets);
    const auto anySounding = std::any_of (other.begin(), other.end(),
                                          [] (int pitch) { return pitch >= 0; });

    if (! anySounding)
    {
        auto& d = diagnostics.warning (
            "W602",
            diagnostics.text (Msg::generator_nothingToAnswer_message,
                              MsgArgs {}.with ("channel", part.counterpoint.against)),
            part.counterpoint.againstRange);
        d.notes.push_back (diagnostics.text (Msg::generator_nothingToAnswer_note));
        return;
    }

    const auto low = part.counterpoint.hasRange ? part.counterpoint.lowPitch : channel.lowPitch;
    const auto high = part.counterpoint.hasRange ? part.counterpoint.highPitch : channel.highPitch;

    // Which side this voice sits on, from the two declared RANGES - the
    // only place the answer is written down. Inferring it from the first
    // note the other voice happens to play gets it wrong whenever the two
    // start close together, and then `voice-crossing forbid` forbids the
    // wrong direction and every note crosses.
    const auto* otherChannel = model.channel (part.counterpoint.against);
    const auto otherMid = otherChannel != nullptr
                              ? (otherChannel->lowPitch + otherChannel->highPitch) / 2
                              : (low + high) / 2;

    const auto ownIsAbove = (low + high) / 2 > otherMid;

    const auto result = generateCounterpoint (onsets, spans, { other }, part.counterpoint, low,
                                              high, ownIsAbove, stepsPerBar,
                                              site.instance.child ("counterpoint"));

    // Relaxation is reported, never silent: a voice that went where it was
    // told not to is something the writer has to know about.
    for (const auto& relaxed : result.relaxations)
        diagnostics.warning (
            "W603",
            diagnostics.text (
                Msg::generator_ruleRelaxed_message,
                MsgArgs {}.with ("rule", nameOf (relaxed.rule)).with ("bar", relaxed.bar)),
            part.counterpoint.againstRange);

    auto ordinal = 0;

    for (const auto& note : result.notes)
    {
        const auto sounded = note.pitch + transpose;

        if (sounded >= lowestPitch && sounded <= highestPitch)
            notes.push_back ({ track, note.startStep, note.lengthSteps, sounded,
                               velocityFor (channel, site, ordinal,
                                            note.startStep / std::max (1, stepsPerBar)) });

        ++ordinal;
    }
}

} // namespace dew::lang
