#include "control/ControlOps.h"

namespace dew::control
{

const std::vector<OpSpec>& ops()
{
    // Built once, on first call, and never rebuilt. A function-local static
    // rather than a namespace-scope one so nothing depends on the order two
    // translation units happen to be initialised in - the same reason every
    // table in model/ is behind a function.
    static const std::vector<OpSpec> table = []
    {
        std::vector<OpSpec> all;

        // The order is the order the reference reads in, so it runs from "what
        // is this project" outwards to "write it to a file" rather than
        // alphabetically. A model reading tools/list top to bottom meets
        // orientation before mutation, which is the order it should work in.
        appendProjectOps (all);
        appendParamOps (all);
        appendChannelOps (all);
        appendEffectOps (all);
        appendMixerOps (all);
        appendPatternOps (all);
        appendNoteOps (all);
        appendPlaylistOps (all);
        appendAutomationOps (all);
        appendScoreOps (all);
        appendTransportOps (all);
        appendOutputOps (all);

        return all;
    }();

    return table;
}

const OpSpec* findOp (juce::StringRef name)
{
    // A linear walk of thirty entries, called once per request over a socket.
    // A map here would be a second structure holding the same fact as the
    // table, and the table is already the thing every other reader walks.
    for (const auto& op : ops())
        if (name == juce::StringRef (op.name))
            return &op;

    return nullptr;
}

ControlResult invoke (ControlHost& host, const OpSpec& op, const juce::var& args)
{
    if (op.edits == OpEdits::yes)
        if (auto* undo = host.undoManager())
            undo->beginNewTransaction (juce::String ("Agent: ") + op.name);

    return op.handler (host, args);
}

} // namespace dew::control
