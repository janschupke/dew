#pragma once

namespace dew::CommandIDs
{

enum
{
    fileNew = 0x2000,
    fileOpen,
    fileSave,
    fileSaveAs,
    editUndo,
    editRedo,
    transportPlayStop,
    transportRewind,
    transportToggleMode,
    addChannel,
    addPattern,
    audioSettings,
    midiSettings,
};

} // namespace dew::CommandIDs
