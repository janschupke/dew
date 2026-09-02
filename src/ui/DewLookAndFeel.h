#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew
{

/** The colour palette and control styling.

    Colours are named once here rather than being written as literals wherever
    they are needed, so the grid, the piano roll, the playlist and the mixer
    stay recognisably one application.
*/
struct Palette
{
    static const juce::Colour background;      ///< window
    static const juce::Colour panel;           ///< raised areas
    static const juce::Colour panelDark;       ///< recessed areas: grids, wells
    static const juce::Colour line;            ///< ordinary grid lines
    static const juce::Colour lineStrong;      ///< bar lines, section edges
    static const juce::Colour text;
    static const juce::Colour textDim;
    static const juce::Colour accent;          ///< selection, focus
    static const juce::Colour playhead;
    static const juce::Colour beat;            ///< beat-boundary cell tint
};

class DewLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DewLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;
};

} // namespace dew
