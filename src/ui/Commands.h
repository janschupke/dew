#pragma once

namespace dew::CommandIDs
{

enum
{
    fileNew = 0x2000,
    fileOpen,
    fileSave,
    fileSaveAs,
    fileRender,
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
