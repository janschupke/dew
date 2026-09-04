// =============================================================================
// The generator's structure: the grid, the tracks, the arrangement, and what
// one section instance turns into.
//
// One of two translation units defining lang/GeneratorImpl.h. The eight
// per-part renderers are the other; this is everything deciding WHERE and HOW
// OFTEN, which then hands each of them a span, a channel and a draw site.
//
// The anonymous namespace below is the pattern cache. Two instances that render
// the same notes get the same pattern index, keyed on a string built from the
// notes themselves - which is what stops a 32-bar song being 32 patterns.
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
                          if (a.startStep != b.startStep)
                              return a.startStep < b.startStep;
                          if (a.track != b.track)
                              return a.track < b.track;
                          return a.pitch < b.pitch;
                      });
}

} // namespace

Score Generator::run()
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

bool Generator::decideGrid()
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

        auto& d = diagnostics.error (
            "E401", "this duration needs a grid of " + std::to_string (needed) + " steps per beat",
            use.range, "requires stepsPerBeat divisible by " + std::to_string (needed));
        d.notes.push_back ("the score declares a grid of " + std::to_string (score.stepsPerBeat));
        d.helps.push_back ("remove `grid` to let the compiler choose one");
        return false;
    }

    return true;
}

void Generator::reportGridTooFine (const GridResolution& resolved)
{
    const auto anchor = resolved.firstWitness.has_value() ? resolved.firstWitness->range
                                                          : model.song.gridRange;

    auto& d = diagnostics.error ("E402",
                                 "this score needs a grid of " + std::to_string (resolved.required)
                                     + " steps per beat, but dew stores at most "
                                     + std::to_string (maxStepsPerBeat),
                                 anchor);

    // BOTH witnesses, because the conflict is between two durations and
    // either one alone would have been fine.
    if (resolved.firstWitness.has_value())
        d.notes.push_back (
            std::to_string (gridNeededFor (resolved.firstWitness->duration, score.beatUnit))
            + " steps per beat are needed here");

    if (resolved.secondWitness.has_value())
        d.related.push_back (
            { resolved.secondWitness->range,
              std::to_string (gridNeededFor (resolved.secondWitness->duration, score.beatUnit))
                  + " steps per beat are needed" });

    d.helps.push_back ("a note value and a triplet of a finer value cannot share "
                       "a grid this coarse");
}

void Generator::buildTracks()
{
    for (const auto& channel : model.channels)
    {
        if ((int) score.tracks.size() >= maxTracks)
        {
            auto& d = diagnostics.error ("E501", "too many channels", channel.range);
            d.notes.push_back ("dew plays at most " + std::to_string (maxTracks));
            return;
        }

        score.tracks.push_back (
            { channel.name, channel.mixerTrack, channel.lowPitch, channel.highPitch });
    }
}

int Generator::trackIndexFor (std::string_view name) const
{
    for (std::size_t i = 0; i < score.tracks.size(); ++i)
        if (score.tracks[i].name == name)
            return (int) i;

    return -1;
}

void Generator::layOutArrangement()
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
            const auto instanceKey = item.label.empty()
                                         ? item.section + "#" + std::to_string (ordinal)
                                         : item.section + "@" + item.label;

            const auto instancePath = item.label.empty() ? SeedPath { model.song.seed }
                                                               .child ("section:" + item.section)
                                                               .child ("instance", ordinal)
                                                         : SeedPath { model.song.seed }
                                                               .child ("section:" + item.section)
                                                               .child ("label:" + item.label);

            auto notes = renderSection (*section, item, instancePath, stepsPerBar);
            sortNotes (notes);

            const auto lengthSteps = section->bars * stepsPerBar;
            const auto pattern = patternFor (item, *section, notes, lengthSteps, ordinal,
                                             instanceKey);

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

int Generator::patternFor (const ArrangementItem& item, const SectionSpec& section,
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

std::vector<Note> Generator::renderSection (const SectionSpec& section, const ArrangementItem& item,
                                            const SeedPath& instancePath, int stepsPerBar)
{
    std::vector<Note> notes;

    const auto* harmonySpec = section.inlineHarmony.has_value() ? &*section.inlineHarmony
                                                                : model.harmony (section.harmony);

    if (harmonySpec == nullptr)
        return notes;

    const auto totalSteps = section.bars * stepsPerBar;

    const auto spans = layOutHarmony (*harmonySpec, model.song.key, totalSteps, stepsPerBar,
                                      score.beatUnit, score.stepsPerBeat, diagnostics);

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

        renderPart (*effective, spans, instancePath, section.name, totalSteps, stepsPerBar, notes);
    }

    return notes;
}

void Generator::recordHarmony (const std::vector<ChordSpan>& spans, int clipIndex, int stepsPerBar)
{
    // Absolute, so the editor can show what sounds where without re-deriving
    // the arrangement.
    const auto offset = clipIndex == 0 && score.clips.empty()
                            ? 0
                            : (score.clips.empty()
                                   ? 0
                                   : (score.clips.back().startBar + score.clips.back().lengthBars)
                                         * stepsPerBar);

    for (const auto& span : spans)
        score.harmony.push_back (
            { offset + span.startStep, offset + span.endStep, span.chord.label });
}

void Generator::renderPart (const PartSpec& part, const std::vector<ChordSpan>& spans,
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
        renderLine (part, *channel, spans, track, transpose, totalSteps, stepsPerBar, notes, site);
    else if (part.kind == PartKind::imitation)
        renderImitation (part, *channel, spans, track, transpose, totalSteps, stepsPerBar, notes,
                         site);
    else if (part.kind == PartKind::counterpoint)
        renderCounterpoint (part, *channel, spans, track, transpose, totalSteps, stepsPerBar, notes,
                            site);
    else
        renderMelody (part, *channel, spans, track, transpose, totalSteps, stepsPerBar, notes,
                      site);

    // A part that writes nothing is almost always a mistake, and it is the
    // one mistake that looks like success from every other angle: the
    // compile passes, the project loads, and one instrument is missing.
    // `rhythmFor` not knowing about a counterpoint's rhythm made an entire
    // voice vanish exactly this quietly.
    if (notes.size() == before)
    {
        auto& d = diagnostics.warning (
            "W604", std::string ("`") + part.channel + "` wrote no notes here", part.range);
        d.helps.push_back ("a part needs a rhythm, and something to play over it");
    }
}

const RhythmSpec* Generator::rhythmFor (const PartSpec& part) const
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

int Generator::chooseDegree (const DegreeChoice& choice, const DrawSite& site)
{
    if (! choice.declared())
        return 0;

    if (choice.degrees.size() == 1)
        return choice.degrees.front();

    auto rng = site.pathFor (choice.scope, "cadence", 0, 0).rng();
    return choice.degrees[rng.below ((std::uint32_t) choice.degrees.size())];
}

float Generator::velocityFor (const ChannelSpec& channel, const DrawSite& site, int ordinal,
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

int Generator::stepsPerBarOf() const noexcept
{
    return score.stepsPerBeat * score.beatsPerBar;
}

Score generate (const Model& model, DiagnosticBag& diagnostics)
{
    Generator generator { model, diagnostics };
    return generator.run();
}

} // namespace dew::lang
