#pragma once

#include <initializer_list>
#include <utility>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include "model/AutomationTargets.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"

/** Naming an automation target, drawing a curve on it, and measuring a window
    of the render that results.

    Shared by the curve tests and the render tests. targetNamed exists so a test
    says WHAT it is automating rather than indexing into a list whose order it
    would then depend on.
*/
namespace dew::testing
{

/** The target with this display name, so tests name what they are automating
    rather than indexing into a list whose order they would then depend on.
*/
inline AutomationTarget targetNamed (const juce::ValueTree& project, const juce::String& name)
{
    for (const auto& target : availableAutomationTargets (project))
        if (target.displayName == name)
            return target;

    FAIL ("no automation target named " << name);
    return {};
}

/** RMS of one window of a render, which is how "did this get louder" is
    actually measured.
*/
inline float rmsOfWindow (const juce::AudioBuffer<float>& buffer, double fromSeconds,
                          double toSeconds, double sampleRate = 44100.0)
{
    const auto start = juce::jlimit (0, buffer.getNumSamples() - 1,
                                     (int) (fromSeconds * sampleRate));
    const auto end = juce::jlimit (start, buffer.getNumSamples(), (int) (toSeconds * sampleRate));

    return end > start ? buffer.getRMSLevel (0, start, end - start) : 0.0f;
}

/** Replaces an automation's curve outright.

    addAutomation seeds two points so a new clip is a line rather than an empty
    box, and those seeds survive adding more - so a test that only adds points
    is testing a shape it did not draw. Learned the hard way.
*/
inline void setCurve (juce::ValueTree automation,
                      std::initializer_list<std::pair<double, double>> curve,
                      juce::UndoManager* undo)
{
    for (const auto& [step, value] : curve)
        ProjectEdits::addAutomationPoint (automation, step, value, undo);

    juce::Array<juce::ValueTree> unwanted;

    for (const auto& point : automation)
    {
        if (! point.hasType (ids::POINT))
            continue;

        bool wanted = false;

        for (const auto& [step, value] : curve)
        {
            juce::ignoreUnused (value);
            wanted = wanted || juce::approximatelyEqual ((double) point[ids::step], step);
        }

        if (! wanted)
            unwanted.add (point);
    }

    for (const auto& point : unwanted)
        ProjectEdits::removeAutomationPoint (automation, point, undo);
}

} // namespace dew::testing
