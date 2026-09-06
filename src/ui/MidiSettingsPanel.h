#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"
#include "io/MidiInputHost.h"
#include "ui/DewDialog.h"
#include "ui/primitives/DewButtons.h"

namespace dew
{

/** Choosing which controllers play, in dew's own idiom.

    The sibling of AudioSettingsPanel, and built the same way: the same tokens,
    the same label-rectangles-drawn-in-paint, the same re-entrancy guard, the
    same live footer under the controls.

    A list of rows rather than a combo box, because several controllers can be
    live at once - a keyboard and a pad grid are not alternatives. A device the
    user has ticked keeps its row when its cable is pulled, greyed and labelled,
    because a tick that silently disappears is a tick the user cannot untick or
    account for.
*/
class MidiSettingsPanel : public dialog::Panel, private juce::Timer
{
public:
    MidiSettingsPanel (MidiInputHost&, Settings* settingsToUpdate = nullptr);
    ~MidiSettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens it in a dialog window owned by JUCE. */
    static void show (MidiInputHost&, Settings*, juce::Component* parent);

    /** Called when a device is enabled or disabled, so the caller can report it. */
    std::function<void()> onDevicesChanged;

    // --- for tests -----------------------------------------------------------
    juce::String getSummaryText() const
    {
        return summaryText;
    }
    int getNumDeviceRows() const
    {
        return rows.size();
    }

    /** The row's label, including its "not connected" suffix. */
    juce::String getDeviceRowText (int index) const;
    bool isDeviceRowTicked (int index) const;

    /** Rebuilds from the host. Normally driven by the host's own callback. */
    void refresh();

    static constexpr int preferredWidth = 420;
    static constexpr int preferredHeight = 340;
    static constexpr int listHeight = 170;

private:
    void timerCallback() override;
    void updateSummary();

    /** One device: a tick and a name. A component per row rather than a
        ListBox, which is what every other multi-row surface in dew does.
    */
    struct DeviceRow : public juce::Component
    {
        DeviceRow (MidiSettingsPanel& o, juce::MidiDeviceInfo i, bool present);

        void resized() override;
        void paint (juce::Graphics&) override;

        MidiSettingsPanel& owner;
        juce::MidiDeviceInfo info;
        bool connected = false;
        DewCheckbox tick;
    };

    MidiInputHost& host;
    Settings* settings = nullptr;

    juce::Viewport listViewport;
    juce::Component listHolder;
    juce::OwnedArray<DeviceRow> rows;

    DewDropdown channelBox, transposeBox;
    juce::Array<juce::Rectangle<int>> labelBounds;
    juce::StringArray labels;

    juce::String summaryText;
    juce::uint32 lastActivity = 0;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiSettingsPanel)
};

} // namespace dew
