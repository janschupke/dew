#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "lang/Harmony.h"
#include "lang/Melody.h"
#include "lang/Model.h"
#include "lang/Rng.h"
#include "lang/Score.h"

namespace dew::lang
{

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
    SeedPath instance { 0 }; ///< song -> section -> instance -> channel

    SeedPath pathFor (Scope scope, std::string_view label, int ordinal, int bar) const
    {
        switch (scope)
        {
            case Scope::song: return SeedPath { songSeed }.child (label);
            case Scope::section:
                return SeedPath { songSeed }.child ("section:" + section).child (label);
            case Scope::instance: return instance.child (label);
            case Scope::bar: return instance.child (label, bar);
            case Scope::note: break;
        }

        return instance.child (label, ordinal);
    }
};

/** The generator, declared so that it can be defined in more than one file.

    INTERNAL. lang/Generator.h is this layer's public surface and declares one
    function; nothing outside the two Generator*.cpp files includes this.
    ResolverImpl.h says the same thing at more length: a file-local class cannot
    be defined in two translation units, and that is the only reason this is
    named at all.

    The split is the eight per-part RENDERERS from everything else. They are the
    leaves - each reads the spans, the channel and the part it was given, draws
    from the site it was handed, and appends notes - and they were 407 of this
    file's 914 lines. DrawSite comes up here with them, because both halves take
    one.
*/
class Generator
{
public:
    Generator (const Model& m, DiagnosticBag& bag)
        : model (m)
        , diagnostics (bag)
    {
    }

    Score run();

private:
    // --- the grid -----------------------------------------------------------
    bool decideGrid();

    void reportGridTooFine (const GridResolution& resolved);

    // --- tracks -------------------------------------------------------------
    void buildTracks();

    int trackIndexFor (std::string_view name) const;

    // --- the arrangement ----------------------------------------------------
    void layOutArrangement();

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
                    const std::string& instanceKey);

    // --- one section instance -----------------------------------------------
    std::vector<Note> renderSection (const SectionSpec& section, const ArrangementItem& item,
                                     const SeedPath& instancePath, int stepsPerBar);

    void recordHarmony (const std::vector<ChordSpan>& spans, int clipIndex, int stepsPerBar);

    void renderPart (const PartSpec& part, const std::vector<ChordSpan>& spans,
                     const SeedPath& instancePath, const std::string& sectionName, int totalSteps,
                     int stepsPerBar, std::vector<Note>& notes);

    const RhythmSpec* rhythmFor (const PartSpec& part) const;

    /** Resolves a choice to one value, at the scope it declares.

        Zero when nothing was declared, which is what "no cadence rule" means
        downstream - a single sentinel rather than an optional threaded through
        the generator.
    */
    static int chooseDegree (const DegreeChoice& choice, const DrawSite& site);

    float velocityFor (const ChannelSpec& channel, const DrawSite& site, int ordinal,
                       int bar) const;

    void renderChords (const PartSpec& part, const ChannelSpec& channel,
                       const std::vector<ChordSpan>& spans, int track, int transpose,
                       std::vector<Note>& notes, const DrawSite& site, int stepsPerBar);

    void renderLine (const PartSpec& part, const ChannelSpec& channel,
                     const std::vector<ChordSpan>& spans, int track, int transpose, int totalSteps,
                     int stepsPerBar, std::vector<Note>& notes, const DrawSite& site);

    static int degreeForSource (LineSource source, int ordinal, std::size_t available);

    void renderMelody (const PartSpec& part, const ChannelSpec& channel,
                       const std::vector<ChordSpan>& spans, int track, int transpose,
                       int totalSteps, int stepsPerBar, std::vector<Note>& notes,
                       const DrawSite& site);

    /** What the voice being answered is sounding at each onset, or -1.

        Read out of the notes ALREADY WRITTEN for this section rather than
        regenerated, so the counterpoint answers exactly what will be heard -
        including anything a per-instance override changed about it.
    */
    std::vector<int> voiceAt (const std::vector<Note>& notes, int track,
                              const std::vector<Onset>& onsets) const;

    void renderImitation (const PartSpec& part, const ChannelSpec& channel,
                          const std::vector<ChordSpan>& spans, int track, int transpose,
                          int totalSteps, int stepsPerBar, std::vector<Note>& notes,
                          const DrawSite& site);

    /** Moves one pitch, by semitones or by scale degrees of the key sounding
        where it lands.

        Diatonic is the default because it is what keeps an answer in the key:
        a chromatic fifth above a minor line is a line in a different mode.
    */
    static int transposePitch (int pitch, const std::vector<ChordSpan>& spans, int step,
                               const ImitationSpec& spec);

    void renderCounterpoint (const PartSpec& part, const ChannelSpec& channel,
                             const std::vector<ChordSpan>& spans, int track, int transpose,
                             int totalSteps, int stepsPerBar, std::vector<Note>& notes,
                             const DrawSite& site);

    int stepsPerBarOf() const noexcept;

    const Model& model;
    DiagnosticBag& diagnostics;
    Score score;
    std::map<std::string, int> patternsByContent;
};

} // namespace dew::lang
