#pragma once

#include <vector>

#include "control/ControlHost.h"
#include "control/ControlTypes.h"

namespace dew::control
{

/** One operation: what it is called, what it may do, what it takes, what it
    says about itself, and the code that runs it.

    A plain function pointer rather than a std::function, so an OpSpec is
    trivially copyable and a table of them is a table rather than thirty heap
    allocations behind type erasure. Nothing here captures.
*/
using Handler = ControlResult (*) (ControlHost&, const juce::var& args);

struct OpSpec
{
    /** snake_case, and the name a client calls. It is also a STORED GRANT in
        every sense that matters: a renamed operation is an operation every
        existing caller silently stops being able to reach, so renaming one is a
        breaking change and not a tidy-up. */
    const char* name = "";

    OpScope scope = OpScope::read;

    /** One line. This is what a model reads when deciding whether to call it,
        so it says what the operation DOES, not what it is called. */
    const char* summary = "";

    /** The paragraphs the website prints and the guide resource quotes:
        when to reach for it, what it refuses, and what it costs in undo steps.
        Written as plain prose with blank lines between paragraphs. */
    const char* doc = "";

    std::vector<ArgSpec> args;

    Handler handler = nullptr;
};

/** Every operation dew exposes, declared once.

    Three readers and deliberately no fourth: the MCP layer builds each tool's
    JSON schema from `args`, the permission check reads `scope`, and the
    emitter behind the website's reference walks the whole table. So an
    operation cannot exist without being completable, and cannot be documented
    differently from how it is checked - the argument lang::schema() already
    makes for the score language, made a second time for the same reason.

    Built once on first call and never rebuilt. The order is the order the
    appenders below run, which is the order the reference reads in.
*/
const std::vector<OpSpec>& ops();

/** The operation of this name, or nullptr. */
const OpSpec* findOp (juce::StringRef name);

// --- the appenders ------------------------------------------------------------
// One per domain, because the table is far past what one file may hold and a
// 400-line limit with no exemption list is not negotiable.
//
// Called explicitly by ops(), rather than each file registering itself from a
// static initialiser. These are static libraries and dew links them without
// --whole-archive, so an object file no symbol reaches is dropped and a
// self-registering table silently loses whichever domains nothing else
// referenced. Hotkeys.cpp says the same thing about the same temptation.

void appendProjectOps (std::vector<OpSpec>&);
void appendParamOps (std::vector<OpSpec>&);
void appendChannelOps (std::vector<OpSpec>&);
void appendEffectOps (std::vector<OpSpec>&);
void appendMixerOps (std::vector<OpSpec>&);
void appendPatternOps (std::vector<OpSpec>&);
void appendNoteOps (std::vector<OpSpec>&);
void appendPlaylistOps (std::vector<OpSpec>&);
void appendAutomationOps (std::vector<OpSpec>&);
void appendScoreOps (std::vector<OpSpec>&);
void appendTransportOps (std::vector<OpSpec>&);
void appendOutputOps (std::vector<OpSpec>&);

} // namespace dew::control
