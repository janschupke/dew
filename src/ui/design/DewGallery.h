#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/SignalScope.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"

namespace dew
{

/** Every token, icon and primitive on one page.

    A design system that only exists as a header is a claim; this makes it
    checkable. `dew_shot gallery` renders it to a PNG for review, and a test
    asserts it paints, so an icon that has been declared but draws nothing, or a
    primitive broken by a token change, shows up here rather than in a feature.
*/
class DewGallery : public juce::Component
{
public:
    DewGallery();
    ~DewGallery() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Height needed to show everything at the current width. Measured from
        the laid-out content rather than guessed, so adding a section cannot
        leave the last one clipped or the page padded with dead space.
    */
    int getRequiredHeight();

private:
    struct Section
    {
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    /** Lays the page out in `area`. With `apply` false it only measures, so the
        required height can be computed without depending on the height the
        component currently happens to have.
    */
    int layOut (juce::Rectangle<int> area, bool apply);

    juce::OwnedArray<juce::Component> controls;
    juce::Array<Section> sections;

    juce::Rectangle<int> paletteBounds, iconBounds;
    int contentBottom = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewGallery)
};

} // namespace dew
