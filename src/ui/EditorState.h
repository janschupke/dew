#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew
{

/** What the editor is looking at, as opposed to what the project contains.

    Deliberately not in the ValueTree: which channel is selected is not part of
    the document, should not be saved, and should not go on the undo stack.
*/
class EditorState : public juce::ChangeBroadcaster
{
public:
    int getSelectedChannelId() const noexcept  { return selectedChannelId; }
    int getCurrentPatternId() const noexcept   { return currentPatternId; }

    void setSelectedChannelId (int id)
    {
        if (std::exchange (selectedChannelId, id) != id)
            sendChangeMessage();
    }

    void setCurrentPatternId (int id)
    {
        if (std::exchange (currentPatternId, id) != id)
            sendChangeMessage();
    }

private:
    int selectedChannelId = 1;
    int currentPatternId = 1;
};

} // namespace dew
