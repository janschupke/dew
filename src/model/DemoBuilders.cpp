#include "model/DemoBuilders.h"

#include <cmath>
#include <vector>

#include "model/DemoSpecs.h"
#include "model/GeneratorCatalog.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "model/TreeWalk.h"

namespace dew::demo
{

double stored (double value)
{
    return std::round (value * 1.0e6) / 1.0e6;
}

juce::ValueTree makeChannel (int id, const juce::String& name, const juce::String& colour,
                             int basePitch, const juce::String& wave, int octave, double attack,
                             double decay, double sustain, double release, double volume)
{
    auto channel = defaultTreeFor (channelsSpec());

    channel.setProperty (ids::id, id, nullptr);
    channel.setProperty (ids::name, name, nullptr);
    channel.setProperty (ids::colour, colour, nullptr);
    channel.setProperty (ids::mixerTrackId, id, nullptr);
    channel.setProperty (ids::basePitch, basePitch, nullptr);
    channel.setProperty (ids::volume, stored (volume), nullptr);

    auto osc = ProjectEdits::oscillatorAt (channel, 0);
    generatorNodeFor (osc, ids::wave).setProperty (ids::wave, wave, nullptr);
    osc.setProperty (ids::octave, octave, nullptr);

    setAmp (channel, attack, decay, sustain, release);

    return channel;
}

juce::ValueTree makeMixerTrack (int id, const juce::String& name)
{
    auto track = defaultTreeFor (childSpecFor (mixerSpec(), "tracks"));
    track.setProperty (ids::id, id, nullptr);
    track.setProperty (ids::name, name, nullptr);
    track.setProperty (ids::gain, 0.8, nullptr);
    track.setProperty (ids::pan, 0.0, nullptr);
    track.setProperty (ids::mute, false, nullptr);
    return track;
}

juce::ValueTree makePlaylistTrack (const juce::String& name)
{
    auto track = defaultTreeFor (tracksSpec());
    track.setProperty (ids::name, name, nullptr);
    return track;
}

juce::ValueTree makePattern (int id, const juce::String& name, int lengthSteps)
{
    auto pattern = defaultTreeFor (patternsSpec());
    pattern.setProperty (ids::id, id, nullptr);
    pattern.setProperty (ids::name, name, nullptr);
    pattern.setProperty (ids::lengthSteps, lengthSteps, nullptr);
    return pattern;
}

juce::ValueTree makeNote (int channelId, int step, int lengthSteps, int pitch, double velocity)
{
    auto note = defaultTreeFor (notesSpec());
    note.setProperty (ids::ch, channelId, nullptr);
    note.setProperty (ids::step, step, nullptr);
    note.setProperty (ids::lengthSteps, lengthSteps, nullptr);
    note.setProperty (ids::pitch, pitch, nullptr);
    note.setProperty (ids::velocity, stored (velocity), nullptr);
    return note;
}

juce::ValueTree patternIn (juce::ValueTree project, int id, const juce::String& name,
                           int lengthSteps)
{
    if (auto pattern = tree::childWithId (project, ids::PATTERN, id); pattern.isValid())
    {
        pattern.setProperty (ids::name, name, nullptr);
        pattern.setProperty (ids::lengthSteps, lengthSteps, nullptr);
        return pattern;
    }

    auto pattern = makePattern (id, name, lengthSteps);
    project.appendChild (pattern, nullptr);
    return pattern;
}

int stepsPerBar (const juce::ValueTree& project)
{
    return Meter::of (project).stepsPerBar();
}

juce::ValueTree makeClipAtStep (int patternId, int startStep, int lengthSteps)
{
    auto clip = defaultTreeFor (clipsSpec());
    clip.setProperty (ids::kind, "pattern", nullptr);
    clip.setProperty (ids::patternId, patternId, nullptr);
    clip.setProperty (ids::startStep, startStep, nullptr);
    clip.setProperty (ids::lengthSteps, lengthSteps, nullptr);
    return clip;
}

juce::ValueTree makeClip (const juce::ValueTree& project, int patternId, int startBar,
                          int lengthBars)
{
    const auto bar = stepsPerBar (project);
    return makeClipAtStep (patternId, startBar * bar, lengthBars * bar);
}

juce::ValueTree makeAutomationClip (const juce::ValueTree& project, int automationId, int startBar,
                                    int lengthBars)
{
    const auto bar = stepsPerBar (project);

    auto clip = defaultTreeFor (clipsSpec());
    clip.setProperty (ids::kind, "automation", nullptr);
    clip.setProperty (ids::automationId, automationId, nullptr);
    clip.setProperty (ids::startStep, startBar * bar, nullptr);
    clip.setProperty (ids::lengthSteps, lengthBars * bar, nullptr);
    return clip;
}

juce::ValueTree makeEffect (int id, const juce::String& type, Params params)
{
    auto effect = defaultTreeFor (effectsSpec());
    effect.setProperty (ids::id, id, nullptr);
    effect.setProperty (ids::type, type, nullptr);

    for (const auto& [property, value] : params)
        effect.setProperty (property, stored (value), nullptr);

    return effect;
}

juce::ValueTree makeFilter (int id, const juce::String& mode, double cutoff, double resonance,
                            double mix)
{
    auto effect = makeEffect (
        id, "filter",
        { { ids::cutoff, cutoff }, { ids::resonance, resonance }, { ids::mix, mix } });
    effect.setProperty (ids::filterMode, mode, nullptr);
    return effect;
}

juce::ValueTree makeDistortion (int id, const juce::String& mode, double drive, double tone,
                                double outputGain, double mix)
{
    auto effect = makeEffect (id, "distortion",
                              { { ids::drive, drive },
                                { ids::tone, tone },
                                { ids::outputGain, outputGain },
                                { ids::mix, mix } });
    effect.setProperty (ids::distortionMode, mode, nullptr);
    return effect;
}

juce::ValueTree makeAutomation (int id, AutomationScope scope, int targetId, int slot,
                                const juce::Identifier& param, std::vector<Point> points)
{
    auto automation = defaultTreeFor (automationSpec());
    automation.setProperty (ids::id, id, nullptr);
    automation.setProperty (ids::scope, automationScopeToString (scope), nullptr);
    automation.setProperty (ids::targetId, targetId, nullptr);
    automation.setProperty (ids::slot, slot, nullptr);
    automation.setProperty (ids::param, param.toString(), nullptr);

    for (const auto& point : points)
    {
        auto node = defaultTreeFor (pointsSpec());
        node.setProperty (ids::step, stored (point.step), nullptr);
        node.setProperty (ids::value, stored (point.value), nullptr);
        node.setProperty (ids::curve, stored (point.curve), nullptr);
        node.setProperty (ids::shape, point.shape, nullptr);
        automation.appendChild (node, nullptr);
    }

    return automation;
}

namespace
{

/** The one place a parameter's normalised form is worked out for a demo.

    Falls back to the value unchanged when the table does not name the
    parameter, because a demo asking for something that is not there is a
    mistake to find in a review rather than a clamp to hide - and 0..1 is what
    every automatable parameter's normalised form already is.
*/
double normalisedIn (const std::vector<ParamSpec>& params, const juce::Identifier& param,
                     double value)
{
    for (const auto& spec : params)
        if (spec.property != nullptr && *spec.property == param)
            return spec.toNormalised (value);

    return value;
}

} // namespace

double curveValue (AutomationScope scope, const juce::Identifier& param, double value)
{
    // Every enumerator, with no default: -Wswitch-enum wants them all even when
    // a default is present, and a scope added later should be a compile error
    // here rather than a demo silently writing raw values into a curve.
    switch (scope)
    {
        case AutomationScope::project: return normalisedIn (projectParams(), param, value);
        case AutomationScope::channel: return normalisedIn (channelParams(), param, value);
        case AutomationScope::channelOsc: return normalisedIn (oscParams(), param, value);
        case AutomationScope::channelAmp: return normalisedIn (ampParams(), param, value);
        case AutomationScope::channelSoundFont:
            return normalisedIn (soundFontParams(), param, value);
        case AutomationScope::mixerTrack: return normalisedIn (mixerTrackParams(), param, value);
        case AutomationScope::master: return normalisedIn (masterParams(), param, value);
        case AutomationScope::channelEffect:
        case AutomationScope::mixerEffect: break;
    }

    // An effect's table depends on its type, which a scope does not carry.
    return value;
}

double curveValueIn (const juce::String& effectType, const juce::Identifier& param, double value)
{
    return normalisedIn (effectParams (effectType), param, value);
}

void notes (juce::ValueTree pattern, int channelId, std::initializer_list<Hit> hits)
{
    for (const auto& hit : hits)
        pattern.appendChild (
            makeNote (channelId, hit.step, hit.lengthSteps, hit.pitch, hit.velocity), nullptr);
}

void chord (juce::ValueTree pattern, int channelId, int step, int lengthSteps,
            std::initializer_list<int> pitches, double velocity)
{
    for (const auto pitch : pitches)
        pattern.appendChild (makeNote (channelId, step, lengthSteps, pitch, velocity), nullptr);
}

void steps (juce::ValueTree pattern, int channelId, int pitch, juce::StringRef grid,
            double velocity, int lengthSteps, int stride, int offset)
{
    auto index = 0;

    for (auto character = grid.text; ! character.isEmpty(); ++character, ++index)
    {
        // An accent and a ghost rather than a second call with a second
        // velocity: a hi-hat part is ONE line of music, and splitting it across
        // three calls is what makes a drum pattern unreadable in source.
        const auto scale = [&]() -> double
        {
            switch (*character)
            {
                case 'X': return 1.25;
                case 'x': return 1.0;
                case 'o': return 0.55;
                default: return 0.0;
            }
        }();

        if (scale <= 0.0)
            continue;

        pattern.appendChild (makeNote (channelId, offset + index * stride, lengthSteps, pitch,
                                       juce::jlimit (0.01, 1.0, velocity * scale)),
                             nullptr);
    }
}

} // namespace dew::demo
