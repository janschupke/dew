#include "model/ProjectFactory.h"

#include "i18n/Strings.h"
#include "model/EntityColour.h"
#include "model/DemoBuilders.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"

namespace dew
{

using namespace demo;

juce::ValueTree ProjectFactory::createDefault (juce::StringRef locale)
{
    const auto numbered = [locale] (StringId id, int number)
    { return trIn (locale, id, Args {}.with ("number", number)); };

    auto project = defaultTreeFor (projectSpec());
    project.setProperty (ids::name, trIn (locale, StringId::project_untitled), nullptr);
    project.setProperty (ids::tempoBpm, 128.0, nullptr);

    // clang-format off
    // Kick and bass sit low; snare is noisy-ish via a fast square; lead sings.
    project.appendChild (makeChannel (1, trIn (locale, StringId::project_kick),
                                      entityColour::defaultHex (0), 36, "sine",     0,
                                      0.001, 0.140, 0.0, 0.060, 0.95), nullptr);
    project.appendChild (makeChannel (2, trIn (locale, StringId::project_snare),
                                      entityColour::defaultHex (1), 60, "square",  -1,
                                      0.001, 0.090, 0.0, 0.060, 0.55), nullptr);
    project.appendChild (makeChannel (3, trIn (locale, StringId::project_bass),
                                      entityColour::defaultHex (2), 40, "saw",      0,
                                      0.004, 0.180, 0.35, 0.090, 0.75), nullptr);
    project.appendChild (makeChannel (4, trIn (locale, StringId::project_lead),
                                      entityColour::defaultHex (3), 72, "triangle", 0,
                                      0.006, 0.150, 0.55, 0.220, 0.60), nullptr);

    // clang-format on
    auto pattern = defaultTreeFor (childSpecFor (projectSpec(), "patterns"));
    pattern.setProperty (ids::id, 1, nullptr);
    pattern.setProperty (ids::name, numbered (StringId::project_patternN, 1), nullptr);
    pattern.setProperty (ids::lengthSteps, 16, nullptr);
    project.appendChild (pattern, nullptr);

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    for (int i = 1; i <= 4; ++i)
        playlist.appendChild (makePlaylistTrack (numbered (StringId::project_trackN, i)), nullptr);

    auto mixer = project.getChildWithName (ids::MIXER);
    for (int i = 1; i <= 4; ++i)
        mixer.appendChild (makeMixerTrack (i, numbered (StringId::project_insertN, i)), nullptr);

    // Children were appended in construction order, not schema order; loading a
    // file always produces schema order, so normalise here or save-then-load
    // would reshape the tree.
    return canonicalTree (project, projectSpec());
}

// clang-format off
const std::vector<ProjectFactory::Demo>& ProjectFactory::demos()
{
    static const std::vector<Demo> library {
        { "demo.dew",       "Getting Started",
          "A four-bar groove: kick, snare, bass and a lead line.", &ProjectFactory::createDemo },
        { "melody.dew",     "Piano Roll",
          "Chords, held notes and a velocity shape.", &ProjectFactory::createMelodyDemo },
        { "effects.dew",    "Effect Chain",
          "A pad through a filter and chorus, a pluck into a delay, reverb on the mix.",
          &ProjectFactory::createEffectsDemo },
        { "automation.dew", "Automation",
          "A filter sweep over four bars, then a fade.", &ProjectFactory::createAutomationDemo },
        { "wavetable.dew",  "Wavetable",
          "Five wavetables, unison and a morph position drawn across thirty-two bars.",
          &ProjectFactory::createWavetableDemo },
        { "layers.dew",     "Oscillator Stack",
          "Three oscillators a channel: octaves, cent detune and per-slot gain.",
          &ProjectFactory::createLayersDemo },
        { "arrangement.dew", "Song Structure",
          "Three lanes coming and going over thirty-two bars, and a drum bus.",
          &ProjectFactory::createArrangementDemo },
        { "amber.dew",      "Compiled from a Score",
          "Fifty-two bars written as text: pads, a bass, a lead and a voice answering it.",
          &ProjectFactory::createScoreDemo },
    };

    // clang-format on
    return library;
}

} // namespace dew
