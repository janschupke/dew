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
    transportRecord,
    addChannel,
    addPattern,
    compileScore,
    audioSettings,
    midiSettings,
};

} // namespace dew::CommandIDs
