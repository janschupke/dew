#include "lang/Generator.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "lang/Counterpoint.h"
#include "lang/Harmony.h"
#include "lang/Melody.h"
#include "lang/Voicing.h"

namespace dew::lang
{

namespace
{

/** dew drops anything past this and reports it; refusing here says so in the
    language's own words instead.
*/
constexpr int maxTracks = 64;

std::string keyOf (const std::vector<Note>& notes)
{
    std::string key;
    key.reserve (notes.size() * 12);

    for (const auto& note : notes)
    {
        key += std::to_string (note.track);
        key += ':';
        key += std::to_string (note.startStep);
        key += ':';
        key += std::to_string (note.lengthSteps);
        key += ':';
        key += std::to_string (note.pitch);
        key += ':';
        key += std::to_string ((int) (note.velocity * 1000.0f));
        key += ';';
    }

    return key;
}

void sortNotes (std::vector<Note>& notes)
{
    std::stable_sort (notes.begin(), notes.end(),
                      [] (const Note& a, const Note& b)
                      {
                          if (a.startStep != b.startStep) return a.startStep < b.startStep;
                          if (a.track != b.track)         return a.track < b.track;
                          return a.pitch < b.pitch;
                      });
}

/** Where a draw sits in the song, so a `per <scope>` value can be keyed on the
    right one of them.

    The scope IS the identity of the draw. `per instance` derives one key for a
    whole rendered instance, so the value cannot change halfway through it and
    cannot be moved by an edit anywhere else in the file; `per note` derives one
    per onset. Same tree, different depth.
*/
struct DrawSite
{
    std::uint64_t songSeed = 0;
    std::string section;
    SeedPath instance { 0 };   ///< song -> section -> instance -> channel

    SeedPath pathFor (Scope scope, std::string_view label, int ordinal, int bar) const
    {
        switch (scope)
        {
            case Scope::song:     return SeedPath { songSeed }.child (label);
            case Scope::section:  return SeedPath { songSeed }.child ("section:" + section)
                                                              .child (label);
            case Scope::instance: return instance.child (label);
            case Scope::bar:      return instance.child (label, bar);
            case Scope::note:     break;
        }

        return instance.child (label, ordinal);
    }
};

class Generator
{
public:
    Generator (const Model& m, DiagnosticBag& bag) : model (m), diagnostics (bag) {}

    Score run()
    {
        score.title = model.song.title;
        score.tempoBpm = model.song.tempoBpm;
        score.beatsPerBar = model.song.meter.beatsPerBar;
        score.beatUnit = model.song.meter.beatUnit;
        score.seed = model.song.seed;

        if (! decideGrid())
            return score;

        buildTracks();

        if (score.tracks.empty())
            return score;

        layOutArrangement();
        return score;
    }

private:
    // --- the grid -----------------------------------------------------------
    bool decideGrid()
    {
        const auto resolved = resolveGrid (model.durationUses, score.beatUnit);

        if (model.song.gridIsAuto)
        {
            if (resolved.exceedsHostLimit)
            {
                reportGridTooFine (resolved);
                return false;
            }

            // At least four, so a bar has somewhere to put an offbeat even when
            // the score only ever writes whole notes.
            score.stepsPerBeat = std::max (4, resolved.stepsPerBeat);
            return true;
        }

        score.stepsPerBeat = model.song.declaredGrid;

        // An explicit grid is a CONTRACT, not a preference: the compiler checks
        // it rather than overriding it, because stepsPerBeat owns how long a
        // step is and silently changing it would alter playback speed.
        for (const auto& use : model.durationUses)
        {
            const auto needed = gridNeededFor (use.duration, score.beatUnit);

            if (score.stepsPerBeat % needed == 0)
                continue;

            auto& d = diagnostics.error ("E401",
                                         "this duration needs a grid of "
                                         + std::to_string (needed) + " steps per beat",
                                         use.range,
                                         "requires stepsPerBeat divisible by "
                                         + std::to_string (needed));
            d.notes.push_back ("the score declares a grid of "
                               + std::to_string (score.stepsPerBeat));
            d.helps.push_back ("remove `grid` to let the compiler choose one");
            return false;
        }

        return true;
    }

    void reportGridTooFine (const GridResolution& resolved)
    {
        const auto anchor = resolved.firstWitness.has_value()
                                ? resolved.firstWitness->range
                                : model.song.gridRange;

        auto& d = diagnostics.error ("E402",
                                     "this score needs a grid of "
                                     + std::to_string (resolved.required)
                                     + " steps per beat, but dew stores at most "
                                     + std::to_string (maxStepsPerBeat),
                                     anchor);

        // BOTH witnesses, because the conflict is between two durations and
        // either one alone would have been fine.
        if (resolved.firstWitness.has_value())
            d.notes.push_back (std::to_string (gridNeededFor (resolved.firstWitness->duration,
                                                              score.beatUnit))
                               + " steps per beat are needed here");

        if (resolved.secondWitness.has_value())
            d.related.push_back ({ resolved.secondWitness->range,
                                   std::to_string (gridNeededFor (resolved.secondWitness->duration,
                                                                  score.beatUnit))
                                   + " steps per beat are needed" });

        d.helps.push_back ("a note value and a triplet of a finer value cannot share "
                           "a grid this coarse");
    }

    // --- tracks -------------------------------------------------------------
    void buildTracks()
    {
        for (const auto& channel : model.channels)
        {
            if ((int) score.tracks.size() >= maxTracks)
            {
                auto& d = diagnostics.error ("E501", "too many channels", channel.range);
                d.notes.push_back ("dew plays at most " + std::to_string (maxTracks));
                return;
            }

            score.tracks.push_back ({ channel.name, channel.mixerTrack,
                                      channel.lowPitch, channel.highPitch });
        }
    }

    int trackIndexFor (std::string_view name) const
    {
        for (std::size_t i = 0; i < score.tracks.size(); ++i)
            if (score.tracks[i].name == name)
                return (int) i;

        return -1;
    }

    // --- the arrangement ----------------------------------------------------
    void layOutArrangement()
    {
        const auto stepsPerBar = score.stepsPerBar();
        auto bar = 0;

        // Counted per section NAME, so a bridge inserted between two verses
        // leaves both verses' choices exactly as they were.
        std::map<std::string, int> ordinals;

        for (const auto& item : model.arrangement)
        {
            const auto* section = model.section (item.section);

            if (section == nullptr)
                continue;

            // `identical` renders once and places the result more than once.
            // Everything else re-rolls, which is what makes a repeat differ
            // wherever randomness exists.
            const auto renders = item.identical ? 1 : item.repeat;
            const auto placements = item.identical ? item.repeat : 1;

            for (auto r = 0; r < renders; ++r)
            {
                const auto ordinal = ordinals[item.section]++;

                // The same two components the seed path is built from, written
                // out so the document can carry them. Keeping them in step is
                // the point: what pins an instance's music must be what pins
                // its pattern, or a recompile would preserve one and not the
                // other.
                const auto instanceKey =
                    item.label.empty() ? item.section + "#" + std::to_string (ordinal)
                                       : item.section + "@" + item.label;

                const auto instancePath =
                    item.label.empty()
                        ? SeedPath { model.song.seed }.child ("section:" + item.section)
                                                      .child ("instance", ordinal)
                        : SeedPath { model.song.seed }.child ("section:" + item.section)
                                                      .child ("label:" + item.label);

                auto notes = renderSection (*section, item, instancePath, stepsPerBar);
                sortNotes (notes);

                const auto lengthSteps = section->bars * stepsPerBar;
                const auto pattern = patternFor (item, *section, notes, lengthSteps,
                                                 ordinal, instanceKey);

                for (auto p = 0; p < placements; ++p)
                {
                    ClipDesc clip;
                    clip.pattern = pattern;
                    clip.startBar = bar;
                    clip.lengthBars = section->bars;
                    clip.label = item.label.empty() ? item.section : item.label;
                    clip.key = instanceKey + "/" + std::to_string (p);

                    score.clips.push_back (clip);
                    bar += section->bars;
                }
            }
        }

        score.barsInSong = std::max (1, bar);
    }

    /** Reuses a pattern whose notes are byte-identical.

        A section repeated with the same content becomes ONE pattern and two
        clips - which is what a musician expects to see in a playlist, and what
        makes "edit it once, hear it twice" work. A repeat that re-rolled its
        randomness produces different notes and therefore a second pattern,
        because there is no honest way to say "the same pattern, different
        notes" in dew's document.
    */
    int patternFor (const ArrangementItem& item, const SectionSpec& section,
                    const std::vector<Note>& notes, int lengthSteps, int ordinal,
                    const std::string& instanceKey)
    {
        const auto key = keyOf (notes);

        // A reused pattern keeps the identity of the instance that FIRST
        // produced it, so its document node does not change hands when an
        // earlier instance is deleted.
        if (const auto found = patternsByContent.find (key); found != patternsByContent.end())
            return found->second;

        PatternDesc pattern;
        pattern.lengthSteps = lengthSteps;
        pattern.notes = notes;
        pattern.key = instanceKey;
        pattern.name = item.label.empty()
                           ? (ordinal == 0 ? section.name
                                           : section.name + " " + std::to_string (ordinal + 1))
                           : item.label;

        const auto index = (int) score.patterns.size();
        score.patterns.push_back (std::move (pattern));
        patternsByContent.emplace (key, index);

        return index;
    }

    // --- one section instance -----------------------------------------------
    std::vector<Note> renderSection (const SectionSpec& section,
                                     const ArrangementItem& item,
                                     const SeedPath& instancePath,
                                     int stepsPerBar)
    {
        std::vector<Note> notes;

        const auto* harmonySpec = section.inlineHarmony.has_value()
                                      ? &*section.inlineHarmony
                                      : model.harmony (section.harmony);

        if (harmonySpec == nullptr)
            return notes;

        const auto totalSteps = section.bars * stepsPerBar;

        const auto spans = layOutHarmony (*harmonySpec, model.song.key, totalSteps,
                                          stepsPerBar, score.beatUnit, score.stepsPerBeat,
                                          diagnostics);

        if (spans.empty())
            return notes;

        recordHarmony (spans, (int) score.clips.size(), stepsPerBar);

        for (const auto& part : section.parts)
        {
            // A per-instance override replaces the part for this instance only.
            const auto* effective = &part;

            for (const auto& override : item.overrides)
                if (override.channel == part.channel)
                    effective = &override;

            renderPart (*effective, spans, instancePath, section.name, totalSteps,
                        stepsPerBar, notes);
        }

        return notes;
    }

    void recordHarmony (const std::vector<ChordSpan>& spans, int clipIndex, int stepsPerBar)
    {
        // Absolute, so the editor can show what sounds where without re-deriving
        // the arrangement.
        const auto offset = clipIndex == 0 && score.clips.empty()
                                ? 0
                                : (score.clips.empty() ? 0
                                                       : (score.clips.back().startBar
                                                          + score.clips.back().lengthBars)
                                                         * stepsPerBar);

        for (const auto& span : spans)
            score.harmony.push_back ({ offset + span.startStep, offset + span.endStep,
                                       span.chord.label });
    }

    void renderPart (const PartSpec& part, const std::vector<ChordSpan>& spans,
                     const SeedPath& instancePath, const std::string& sectionName,
                     int totalSteps, int stepsPerBar, std::vector<Note>& notes)
    {
        const auto track = trackIndexFor (part.channel);

        if (track < 0)
            return;

        const auto* channel = model.channel (part.channel);

        if (channel == nullptr)
            return;

        const auto before = notes.size();

        const auto transpose = 12 * (part.octave + channel->octave);

        DrawSite site;
        site.songSeed = model.song.seed;
        site.section = sectionName;
        site.instance = instancePath.child ("channel:" + part.channel);

        if (part.kind == PartKind::chords)
            renderChords (part, *channel, spans, track, transpose, notes, site, stepsPerBar);
        else if (part.kind == PartKind::line)
            renderLine (part, *channel, spans, track, transpose, totalSteps, stepsPerBar,
                        notes, site);
        else if (part.kind == PartKind::imitation)
            renderImitation (part, *channel, spans, track, transpose, totalSteps,
                             stepsPerBar, notes, site);
        else if (part.kind == PartKind::counterpoint)
            renderCounterpoint (part, *channel, spans, track, transpose, totalSteps,
                                stepsPerBar, notes, site);
        else
            renderMelody (part, *channel, spans, track, transpose, totalSteps, stepsPerBar,
                          notes, site);

        // A part that writes nothing is almost always a mistake, and it is the
        // one mistake that looks like success from every other angle: the
        // compile passes, the project loads, and one instrument is missing.
        // `rhythmFor` not knowing about a counterpoint's rhythm made an entire
        // voice vanish exactly this quietly.
        if (notes.size() == before)
        {
            auto& d = diagnostics.warning ("W604",
                                           std::string ("`") + part.channel
                                           + "` wrote no notes here",
                                           part.range);
            d.helps.push_back ("a part needs a rhythm, and something to play over it");
        }
    }

    const RhythmSpec* rhythmFor (const PartSpec& part) const
    {
        if (part.inlineRhythm.has_value())
            return &*part.inlineRhythm;

        if (! part.rhythm.empty())
            return model.rhythm (part.rhythm);

        if (! part.melody.rhythm.empty())
            return model.rhythm (part.melody.rhythm);

        if (! part.counterpoint.rhythm.empty())
            return model.rhythm (part.counterpoint.rhythm);

        return nullptr;
    }

    /** Resolves a choice to one value, at the scope it declares.

        Zero when nothing was declared, which is what "no cadence rule" means
        downstream - a single sentinel rather than an optional threaded through
        the generator.
    */
    static int chooseDegree (const DegreeChoice& choice, const DrawSite& site)
    {
        if (! choice.declared())
            return 0;

        if (choice.degrees.size() == 1)
            return choice.degrees.front();

        auto rng = site.pathFor (choice.scope, "cadence", 0, 0).rng();
        return choice.degrees[rng.below ((std::uint32_t) choice.degrees.size())];
    }

    float velocityFor (const ChannelSpec& channel, const DrawSite& site, int ordinal,
                       int bar) const
    {
        auto value = (float) channel.velocity;

        if (channel.velocityJitter > 0)
        {
            auto rng = site.pathFor (channel.velocityScope, "velocity", ordinal, bar).rng();
            value += rng.jitter ((float) channel.velocityJitter);
        }

        // Clamped to at least one: zero IS a note-off in MIDI.
        const auto normalised = std::clamp (value, 1.0f, 127.0f) / 127.0f;

        // Quantised to three decimals, which is not cosmetic. dew writes its
        // JSON with six decimal places, so a velocity of 68/127 =
        // 0.5354330708... is written as 0.535433 and read back as a DIFFERENT
        // double - and the project then fails to round-trip through its own
        // file format. Three decimals is a thousand levels for a value that
        // only ever had 127, and it survives the writer exactly.
        return std::round (normalised * 1000.0f) / 1000.0f;
    }

    void renderChords (const PartSpec& part, const ChannelSpec& channel,
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
                onsets.push_back ({ 0, span.endStep - span.startStep, false, false,
                                    MetricStrength::barStart });

            for (const auto& onset : onsets)
            {
                if (onset.isRest)
                    continue;

                for (const auto pitch : pitches)
                {
                    const auto sounded = pitch + transpose;

                    if (sounded < lowestPitch || sounded > highestPitch)
                        continue;

                    notes.push_back ({ track, span.startStep + onset.startStep,
                                       onset.lengthSteps, sounded,
                                       velocityFor (channel, site, ordinal,
                                                    (span.startStep + onset.startStep)
                                                        / std::max (1, stepsPerBar)) });
                }

                ++ordinal;
            }
        }
    }

    void renderLine (const PartSpec& part, const ChannelSpec& channel,
                     const std::vector<ChordSpan>& spans, int track, int transpose,
                     int totalSteps, int stepsPerBar, std::vector<Note>& notes,
                     const DrawSite& site)
    {
        const auto* rhythm = rhythmFor (part);

        std::vector<Onset> onsets;

        if (rhythm != nullptr)
            onsets = tileRhythm (*rhythm, totalSteps, stepsPerBar, score.beatUnit,
                                 score.stepsPerBeat);

        if (onsets.empty())
            for (const auto& span : spans)
                onsets.push_back ({ span.startStep, span.endStep - span.startStep,
                                    false, false, MetricStrength::barStart });

        auto ordinal = 0;

        for (const auto& onset : onsets)
        {
            if (onset.isRest)
                continue;

            const ChordSpan* span = nullptr;

            for (const auto& candidate : spans)
                if (onset.startStep >= candidate.startStep
                    && onset.startStep < candidate.endStep)
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
                                                onset.startStep
                                                    / std::max (1, stepsPerBar)) });

            ++ordinal;
        }
    }

    static int degreeForSource (LineSource source, int ordinal, std::size_t available)
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

    void renderMelody (const PartSpec& part, const ChannelSpec& channel,
                       const std::vector<ChordSpan>& spans, int track, int transpose,
                       int totalSteps, int stepsPerBar, std::vector<Note>& notes,
                       const DrawSite& site)
    {
        const auto* rhythm = rhythmFor (part);

        if (rhythm == nullptr)
            return;

        auto onsets = tileRhythm (*rhythm, totalSteps, stepsPerBar, score.beatUnit,
                                  score.stepsPerBeat,
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
                                                note.startStep
                                                    / std::max (1, stepsPerBar)) });

            ++ordinal;
        }
    }

    /** What the voice being answered is sounding at each onset, or -1.

        Read out of the notes ALREADY WRITTEN for this section rather than
        regenerated, so the counterpoint answers exactly what will be heard -
        including anything a per-instance override changed about it.
    */
    std::vector<int> voiceAt (const std::vector<Note>& notes, int track,
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

    void renderImitation (const PartSpec& part, const ChannelSpec& channel,
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
            auto& d = diagnostics.warning ("W605",
                                           "`" + part.imitation.source
                                           + "` has nothing to imitate here",
                                           part.imitation.sourceRange);
            d.notes.push_back ("parts are written in the order they are declared, so the "
                               "voice being copied has to come first");
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

            const auto shifted = transposePitch (note.pitch, spans, start,
                                                 part.imitation);
            const auto sounded = shifted + transpose;

            if (sounded < channel.lowPitch || sounded > channel.highPitch)
                continue;

            if (sounded < lowestPitch || sounded > highestPitch)
                continue;

            notes.push_back ({ track, start,
                               std::min (note.lengthSteps, totalSteps - start), sounded,
                               velocityFor (channel, site, ordinal,
                                            start / std::max (1, stepsPerBar)) });
            ++ordinal;
        }
    }

    /** Moves one pitch, by semitones or by scale degrees of the key sounding
        where it lands.

        Diatonic is the default because it is what keeps an answer in the key:
        a chromatic fifth above a minor line is a line in a different mode.
    */
    static int transposePitch (int pitch, const std::vector<ChordSpan>& spans, int step,
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

    void renderCounterpoint (const PartSpec& part, const ChannelSpec& channel,
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
                                        score.stepsPerBeat,
                                        part.counterpoint.align == Alignment::bar);

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
            auto& d = diagnostics.warning ("W602",
                                           "`" + part.counterpoint.against
                                           + "` has nothing to answer here",
                                           part.counterpoint.againstRange);
            d.notes.push_back ("parts are written in the order they are declared, so the "
                               "voice being answered has to come first");
            return;
        }

        const auto low = part.counterpoint.hasRange ? part.counterpoint.lowPitch
                                                    : channel.lowPitch;
        const auto high = part.counterpoint.hasRange ? part.counterpoint.highPitch
                                                     : channel.highPitch;

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

        const auto result = generateCounterpoint (onsets, spans, { other },
                                                  part.counterpoint, low, high, ownIsAbove,
                                                  stepsPerBar,
                                                  site.instance.child ("counterpoint"));

        // Relaxation is reported, never silent: a voice that went where it was
        // told not to is something the writer has to know about.
        for (const auto& relaxed : result.relaxations)
            diagnostics.warning ("W603",
                                 std::string ("`") + nameOf (relaxed.rule)
                                 + "` had to be given up in bar "
                                 + std::to_string (relaxed.bar),
                                 part.counterpoint.againstRange);

        auto ordinal = 0;

        for (const auto& note : result.notes)
        {
            const auto sounded = note.pitch + transpose;

            if (sounded >= lowestPitch && sounded <= highestPitch)
                notes.push_back ({ track, note.startStep, note.lengthSteps, sounded,
                                   velocityFor (channel, site, ordinal,
                                                note.startStep
                                                    / std::max (1, stepsPerBar)) });

            ++ordinal;
        }
    }

    int stepsPerBarOf() const noexcept { return score.stepsPerBeat * score.beatsPerBar; }

    const Model& model;
    DiagnosticBag& diagnostics;
    Score score;
    std::map<std::string, int> patternsByContent;
};

} // namespace

// ------------------------------------------------------------------------------

std::vector<Note> Score::flatten() const
{
    std::vector<Note> out;

    for (const auto& clip : clips)
    {
        if (clip.pattern < 0 || clip.pattern >= (int) patterns.size())
            continue;

        const auto& pattern = patterns[(std::size_t) clip.pattern];
        const auto offset = clip.startBar * stepsPerBar();

        for (const auto& note : pattern.notes)
        {
            auto moved = note;
            moved.startStep = offset + note.startStep;
            out.push_back (moved);
        }
    }

    std::stable_sort (out.begin(), out.end(),
                      [] (const Note& a, const Note& b)
                      {
                          if (a.startStep != b.startStep) return a.startStep < b.startStep;
                          if (a.track != b.track)         return a.track < b.track;
                          return a.pitch < b.pitch;
                      });

    return out;
}

int Score::noteCount() const noexcept
{
    auto count = 0;

    for (const auto& clip : clips)
        if (clip.pattern >= 0 && clip.pattern < (int) patterns.size())
            count += (int) patterns[(std::size_t) clip.pattern].notes.size();

    return count;
}

Score generate (const Model& model, DiagnosticBag& diagnostics)
{
    Generator generator { model, diagnostics };
    return generator.run();
}

} // namespace dew::lang
