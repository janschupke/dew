#include "ui/SampleSection.h"

#include "i18n/Strings.h"
#include "model/ProjectEdits.h"
#include "ui/KnobGrid.h"
#include "ui/design/Cursors.h"
#include "ui/design/Tokens.h"

namespace dew
{

namespace
{
/** Half the width of a trim handle's grab area, in pixels. */
constexpr float handleGrabRadius = 6.0f;
} // namespace

SampleSection::SampleSection (ProjectDocument& d, SamplePool* p)
    : document (d)
    , pool (p)
{
    setComponentID ("sampleSection");
    // A name and a PLACE in the tree a screen reader is given. focusContainer,
    // not keyboardFocusContainer: the two flags are independent, and the second
    // would confine the tab key to this panel with no key to leave it - a
    // keyboard trap, which is worse than the flat tab order it would tidy.
    setTitle (tr (StringId::sample_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    fadeInKnob.setNumDecimalPlaces (0);
    fadeOutKnob.setNumDecimalPlaces (0);
    transposeKnob.setNumDecimalPlaces (0);
    transposeKnob.setBipolar (true);

    attachKnob (fadeInKnob, ids::fadeInMs, "Change fade in");
    attachKnob (fadeOutKnob, ids::fadeOutMs, "Change fade out");
    attachKnob (transposeKnob, ids::transpose, "Change sample pitch");

    reverseButton.setClickingTogglesState (true);
    reverseButton.onClick = [this]
    {
        if (! updating)
            write (ids::reverse, reverseButton.getToggleState(), "Reverse sample", false);
    };
    addAndMakeVisible (reverseButton);

    loopButton.setClickingTogglesState (true);
    loopButton.onClick = [this]
    {
        if (! updating)
            write (ids::loop, loopButton.getToggleState(), "Loop sample", false);
    };
    addAndMakeVisible (loopButton);

    document.getState().addListener (this);
}

SampleSection::~SampleSection()
{
    document.getState().removeListener (this);
}

void SampleSection::setParamMenuHost (const paramMenu::Host* host)
{
    paramMenuHost = host;

    const std::function<juce::ValueTree()> self = [this] { return sample; };

    paramMenu::attachTo (host, fadeInKnob, self, requireInstrumentParamSpec (ids::fadeInMs));
    paramMenu::attachTo (host, fadeOutKnob, self, requireInstrumentParamSpec (ids::fadeOutMs));
    paramMenu::attachTo (host, transposeKnob, self, requireInstrumentParamSpec (ids::transpose));

    // Neither is automatable - reversing a sample is a discontinuity in a read
    // pointer, not a value - so what the menu offers them is a reset. That is
    // the other half of why a control has one, and these two were the only
    // spec-built controls in the section without it.
    paramMenu::attachTo (host, reverseButton, self, requireInstrumentParamSpec (ids::reverse));
    paramMenu::attachTo (host, loopButton, self, requireInstrumentParamSpec (ids::loop));
}

void SampleSection::attachKnob (DewKnob& knob, const juce::Identifier& property,
                                const juce::String& transactionName)
{
    gesture.attach (knob,
                    [this, &knob, property, transactionName] (bool continuing)
                    {
                        if (updating)
                            return false;

                        write (property, knob.getValue(), transactionName, continuing);
                        return true;
                    });

    addAndMakeVisible (knob);
}

void SampleSection::write (const juce::Identifier& property, const juce::var& value,
                           const juce::String& transactionName, bool continuing)
{
    ProjectEdits::setProperty (sample, property, value, &document.getUndoManager(), transactionName,
                               continuing);
}

void SampleSection::setOwner (juce::ValueTree sampleNode)
{
    sample = std::move (sampleNode);
    refresh();
}

const SamplePool::Entry* SampleSection::entry() const
{
    if (pool == nullptr || ! sample.isValid())
        return nullptr;

    const auto path = sample[ids::file].toString();

    if (path.isEmpty())
        return nullptr;

    const auto& found = pool->loadReference (path);
    return found.isValid() ? &found : nullptr;
}

int SampleSection::sourceLength() const
{
    if (const auto* found = entry())
        return found->audio->getNumSamples();

    return 0;
}

bool SampleSection::hasAudio() const
{
    return sourceLength() > 0;
}

void SampleSection::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    // By identity, not by type: this listens to the whole document, and every
    // other channel has a SAMPLE node too.
    if (tree == sample)
        refresh();
}

void SampleSection::refresh()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    const auto valid = sample.isValid();
    setEnabled (valid);

    if (valid)
    {
        fadeInKnob.setValue ((double) sample[ids::fadeInMs], juce::dontSendNotification);
        fadeOutKnob.setValue ((double) sample[ids::fadeOutMs], juce::dontSendNotification);
        transposeKnob.setValue ((double) sample[ids::transpose], juce::dontSendNotification);
        reverseButton.setToggleState ((bool) sample[ids::reverse], juce::dontSendNotification);
        loopButton.setToggleState ((bool) sample[ids::loop], juce::dontSendNotification);
    }

    repaint();
}

juce::Rectangle<int> SampleSection::getWaveformBounds() const
{
    return getLocalBounds().removeFromTop (waveformHeight);
}

void SampleSection::resized()
{
    using namespace tokens;

    auto area = getLocalBounds();
    area.removeFromTop (waveformHeight);
    area.removeFromTop (space::sm);

    // The fades are a pair and the transpose knob stands alone beside the
    // toggles, so they are two rows rather than a grid - but at the cell width
    // every other knob in the panel gets, instead of half the section each.
    const auto placeKnobs = [] (juce::Rectangle<int> bounds, std::initializer_list<DewKnob*> knobs)
    {
        const std::vector<int> group { (int) knobs.size() };
        const auto placed = KnobGrid::place (bounds,
                                             KnobGrid::planForRows (1, group, bounds.getWidth()));

        auto cell = placed.cells.begin();

        for (auto* knob : knobs)
            if (cell != placed.cells.end())
                knob->setBounds (*cell++);
    };

    placeKnobs (area.removeFromTop (size::knobRow), { &fadeInKnob, &fadeOutKnob });

    area.removeFromTop (space::sm);

    auto bottom = area.removeFromTop (size::knobRow);
    placeKnobs (bottom.removeFromLeft (bottom.getWidth() / 2), { &transposeKnob });

    // The two toggles stack in the space one knob would take, centred against
    // the knob beside them rather than filling the row - a full-width button
    // next to a 44px knob reads as the more important control, and it is not.
    auto toggles = bottom.withSizeKeepingCentre (size::iconButton,
                                                 size::iconButton * 2 + space::xs);
    reverseButton.setBounds (toggles.removeFromTop (size::iconButton));
    toggles.removeFromTop (space::xs);
    loopButton.setBounds (toggles.removeFromTop (size::iconButton));
}

float SampleSection::getTrimHandleX (bool start) const
{
    const auto bounds = getWaveformBounds().toFloat();
    const auto length = sourceLength();

    if (length <= 0)
        return bounds.getX();

    const auto stored = start ? (int) sample[ids::startSample] : (int) sample[ids::endSample];
    const auto frame = start ? juce::jlimit (0, length, stored)
                             : (stored <= 0 ? length : juce::jlimit (0, length, stored));

    return bounds.getX() + bounds.getWidth() * (float) frame / (float) length;
}

int SampleSection::sampleAtX (float x) const
{
    const auto bounds = getWaveformBounds().toFloat();
    const auto length = sourceLength();

    if (length <= 0 || bounds.getWidth() <= 0.0f)
        return 0;

    const auto fraction = juce::jlimit (0.0f, 1.0f, (x - bounds.getX()) / bounds.getWidth());
    return juce::jlimit (0, length, (int) (fraction * (float) length));
}

SampleSection::Handle SampleSection::handleAt (juce::Point<float> position) const
{
    if (! hasAudio() || ! getWaveformBounds().toFloat().contains (position))
        return Handle::none;

    if (std::abs (position.x - getTrimHandleX (true)) <= handleGrabRadius)
        return Handle::start;

    if (std::abs (position.x - getTrimHandleX (false)) <= handleGrabRadius)
        return Handle::end;

    return Handle::none;
}

void SampleSection::mouseMove (const juce::MouseEvent& event)
{
    const auto over = handleAt (event.position);

    if (std::exchange (hovering, over) != over)
    {
        setMouseCursor (over == Handle::none ? cursor::idle : cursor::resizeX);
        repaint (getWaveformBounds());
    }
}

void SampleSection::mouseDown (const juce::MouseEvent& event)
{
    // A right press on a trim handle opened a "Trim sample" transaction and
    // armed the drag, so a right-drag moved the trim and left an undo step
    // named after a gesture nobody made.
    if (event.mods.isPopupMenu())
    {
        dragging = Handle::none;
        return;
    }

    dragging = handleAt (event.position);

    if (dragging != Handle::none)
        document.getUndoManager().beginNewTransaction ("Trim sample");
}

void SampleSection::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging == Handle::none || ! sample.isValid())
        return;

    const auto length = sourceLength();
    const auto frame = sampleAtX (event.position.x);

    auto& undo = document.getUndoManager();

    // One frame of separation enforced here rather than clamped at render time:
    // handles that can cross would let a drag produce a region the engine has
    // to silently reinterpret, and the user would see no reason for it.
    if (dragging == Handle::start)
    {
        const auto stored = (int) sample[ids::endSample];
        const auto end = stored <= 0 ? length : stored;

        ProjectEdits::setProperty (sample, ids::startSample,
                                   juce::jlimit (0, juce::jmax (0, end - 1), frame), &undo,
                                   "Trim sample", trimming);
    }
    else
    {
        const auto start = (int) sample[ids::startSample];
        const auto clamped = juce::jlimit (juce::jmin (start + 1, length), length, frame);

        // Storing the full length as 0 keeps "untrimmed" one value rather than
        // two, so a sample replaced by a longer one still plays to its end.
        ProjectEdits::setProperty (sample, ids::endSample, clamped >= length ? 0 : clamped, &undo,
                                   "Trim sample", trimming);
    }

    // The whole sweep of a handle is one undo step, the same as a knob's.
    trimming = true;
}

void SampleSection::mouseUp (const juce::MouseEvent&)
{
    trimming = false;
    dragging = Handle::none;
}

void SampleSection::paint (juce::Graphics& g)
{
    using namespace tokens;

    const auto bounds = getWaveformBounds().toFloat();

    paint::wellBackground (g, getWaveformBounds());

    const auto* found = entry();

    if (found == nullptr)
    {
        paint::emptyState (g, getWaveformBounds(),
                           sample.isValid() ? "No audio - arm this channel and record"
                                            : "No channel selected");
        return;
    }

    const auto startX = getTrimHandleX (true);
    const auto endX = getTrimHandleX (false);

    const juce::Range<float> span { bounds.getX(), bounds.getRight() };

    // Outside the trim, the waveform is still drawn but dimmed: what was
    // trimmed away is context, and hiding it makes a mis-drag look like audio
    // that has been destroyed.
    paint::waveform (g, { bounds.getY(), bounds.getBottom() }, span, span, found->peaks,
                     [startX, endX] (float x)
                     {
                         return x >= startX && x <= endX
                                    ? colour::accent
                                    : colour::textDisabled.withAlpha (emphasis::subdued);
                     });

    // The handles last, so they are never buried under a loud waveform.
    for (const auto start : { true, false })
    {
        const auto x = getTrimHandleX (start);
        const auto active = hovering == (start ? Handle::start : Handle::end)
                            || dragging == (start ? Handle::start : Handle::end);

        g.setColour (active ? colour::textPrimary : colour::playhead);
        g.fillRect (x - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
    }

    g.setColour (colour::outline);
    g.drawRect (bounds, stroke::hairline);
}

} // namespace dew
