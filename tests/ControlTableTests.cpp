#include <set>

#include <catch2/catch_test_macros.hpp>

#include "ControlHarness.h"
#include "control/ControlGuide.h"
#include "control/McpProtocol.h"
#include "control/ParamAddress.h"

using namespace dew;
using namespace dew::control;
using namespace dew::testing;

namespace
{

/** Every argument of every operation, however deeply nested. */
void walkArgs (const std::vector<ArgSpec>& args, const std::function<void (const ArgSpec&)>& visit)
{
    for (const auto& arg : args)
    {
        visit (arg);
        walkArgs (arg.fields, visit);
    }
}

} // namespace

TEST_CASE ("the operation table is not empty and every name is unique", "[control][gate]")
{
    // The control case. A gate over an empty list is not a gate, and a table
    // assembled by eleven explicit appenders is exactly the shape where one
    // forgotten call would leave a whole domain unreachable and say nothing.
    REQUIRE (ops().size() > 25);

    std::set<juce::String> names;

    for (const auto& op : ops())
    {
        INFO ("duplicate operation name: " << op.name);
        REQUIRE (names.insert (op.name).second);
    }
}

TEST_CASE ("every operation reached by name is the one in the table", "[control][gate]")
{
    for (const auto& op : ops())
        REQUIRE (findOp (op.name) == &op);

    REQUIRE (findOp ("no_such_operation") == nullptr);
}

TEST_CASE ("every operation documents itself and every argument it takes", "[control][gate]")
{
    // The whole reason the table is a table: an operation cannot exist without
    // being documented, because the same rows the dispatcher reads are the rows
    // the website prints. A summary nobody wrote would ship as a blank line on
    // a public page.
    for (const auto& op : ops())
    {
        INFO ("operation: " << op.name);

        REQUIRE (juce::String (op.name).isNotEmpty());
        REQUIRE (juce::String (op.summary).length() > 20);
        REQUIRE (juce::String (op.doc).length() > 40);
        REQUIRE (op.handler != nullptr);

        // snake_case, and nothing else. A tool name is a stored grant in every
        // sense that matters, so the spelling is worth holding.
        REQUIRE (juce::String (op.name).toLowerCase() == juce::String (op.name));
        REQUIRE_FALSE (juce::String (op.name).containsChar ('-'));

        walkArgs (op.args,
                  [&op] (const ArgSpec& arg)
                  {
                      INFO ("argument of " << op.name << ": " << arg.name);
                      REQUIRE (juce::String (arg.doc).length() > 8);

                      // A nested shape is what an object or an array of objects
                      // IS. One with no fields is an undescribed blob, which a
                      // model cannot fill correctly however good its guess.
                      if (arg.kind == ValueKind::object)
                          REQUIRE_FALSE (arg.fields.empty());

                      if (arg.kind == ValueKind::array)
                          REQUIRE_FALSE (arg.fields.empty());

                      if (arg.kind != ValueKind::object && arg.kind != ValueKind::array)
                          REQUIRE (arg.fields.empty());
                  });
    }
}

TEST_CASE ("a write operation is named like one", "[control][gate]")
{
    // A naming convention, and only that. It used to be the whole of the scope
    // gate in both directions, and the read direction asked whether the name
    // contained "_write" - so an operation called channels_remove, declared
    // read, took the else branch and passed. A read-only grant could delete
    // channels and this file said it was fine.
    //
    // What a read operation may DO is asserted below, by running it. This half
    // stays because a write whose name reads like a query is a trap for the
    // model choosing between them, which no behavioural test can see.
    for (const auto& op : ops())
    {
        if (op.scope != OpScope::write)
            continue;

        const juce::String name { op.name };

        INFO ("operation: " << op.name);
        REQUIRE ((name.contains ("_write") || name.contains ("_remove") || name.contains ("_move")
                  || name.contains ("_compile") || name.contains ("_transform")
                  || name.contains ("_command") || name.startsWith ("render_")
                  || name.startsWith ("export_")));
    }

    // render_status is the exception the rule above has to allow: it starts
    // with render_ and only reads. Asserted rather than left implied, so the
    // predicate cannot quietly stop covering the rest.
    REQUIRE (findOp ("render_status")->scope == OpScope::read);
}

TEST_CASE ("only a write operation edits the document, and every other write says so",
           "[control][gate]")
{
    // OpEdits decides whether a call opens an undo transaction, and it defaults
    // to `no` - so the failure to guard against is a new editing operation that
    // never states it and quietly stops being one undo step.
    //
    // The four below are the whole of "needs a write grant, does not touch the
    // tree": two write a file, one moves the playhead, one replaces or rewinds
    // the document rather than editing it. Named rather than inferred, so a
    // fifth cannot join them by being forgotten.
    const std::set<juce::String> writesNothing { "project_command", "transport_write",
                                                 "render_audio", "export_midi" };

    for (const auto& op : ops())
    {
        INFO ("operation: " << op.name);

        if (op.edits == OpEdits::yes)
            REQUIRE (op.scope == OpScope::write);

        if (op.scope == OpScope::write)
            REQUIRE ((op.edits == OpEdits::yes) == (writesNothing.count (op.name) == 0));
    }

    for (const auto& name : writesNothing)
    {
        INFO ("operation: " << name);
        REQUIRE (findOp (name) != nullptr);
    }
}

TEST_CASE ("a read operation leaves the document and the undo history alone",
           "[control][gate][mcp]")
{
    // OpScope is the ONLY permission gate: a client approved for reading is
    // allowed to call every operation declared read, and nothing else stands
    // between it and the document. So this asks the question the way the grant
    // does - by running each of them and looking at what moved.
    //
    // Arguments per operation rather than empty ones, because an operation that
    // refuses its way out writes nothing whatever its handler would have done,
    // and a gate every case fails is a gate measuring nothing. Each row reaches
    // something real in the fixture.
    FakeHost host;

    // A curve to read, made through the operation that makes one, so
    // automation_read answers rather than reporting a missing id.
    const auto made = call (
        host, "automation_write",
        Fields {}.with ("target", "channel").with ("id", 1).with ("param", "volume"));
    INFO (made.error);
    REQUIRE (made.ok);

    const auto automationId = (int) made.value[juce::Identifier ("id")];

    const auto
        address = Fields {}.with ("target", "channel").with ("id", 1).with ("param", "volume");

    struct ReadCase
    {
        const char* name;
        juce::var args;
        bool answers; ///< False for the two that need a facility a headless host has not got.
    };

    const std::vector<ReadCase> cases {
        { "project_describe", {}, true },
        { "project_read", {}, true },
        { "params_list", address, true },
        { "params_read", Fields {}.with ("entries", list ({ address })), true },
        { "presets_list", {}, true },
        { "patterns_read", Fields {}.with ("id", 1), true },
        { "automation_targets_list", {}, true },
        { "automation_read", Fields {}.with ("id", automationId), true },
        { "score_read", {}, true },
        { "transport_read", {}, false },
        { "render_status", {}, false },
    };

    // Covered both ways, so the gate prunes itself: a read operation added
    // without a row here fails the first loop, and a row naming an operation
    // that is gone - or that has since become a write - fails the second.
    juce::StringArray covered;

    for (const auto& c : cases)
        covered.add (c.name);

    for (const auto& op : ops())
        if (op.scope == OpScope::read)
        {
            INFO ("no row drives read operation: " << op.name);
            REQUIRE (covered.contains (op.name));
        }

    for (const auto& c : cases)
    {
        INFO ("row names no read operation: " << c.name);
        REQUIRE (findOp (c.name) != nullptr);
        REQUIRE (findOp (c.name)->scope == OpScope::read);
    }

    for (const auto& c : cases)
    {
        INFO ("operation: " << c.name);

        const auto before = host.state.createCopy();
        const auto depthBefore = host.undoDepth();

        const auto result = call (host, c.name, c.args);

        INFO ("said: " << result.error);
        CHECK (result.ok == c.answers);

        // isEquivalentTo compares properties and children, which is the whole
        // of what a saved file holds - a read that touched anything at all
        // moves one of them.
        CHECK (host.state.isEquivalentTo (before));
        CHECK (host.undoDepth() == depthBefore);
    }
}

TEST_CASE ("every operation's schema is one a client can read", "[control][gate][mcp]")
{
    const auto listed = mcp::toolsList();
    const auto* tools = listed[juce::Identifier ("tools")].getArray();

    REQUIRE (tools != nullptr);
    REQUIRE ((std::size_t) tools->size() == ops().size());

    for (int i = 0; i < tools->size(); ++i)
    {
        const auto& tool = tools->getReference (i);
        const auto name = tool[juce::Identifier ("name")].toString();

        INFO ("tool: " << name);

        // The runtime schema and the table are one fact. This is what stops
        // the emitter behind the website drifting from what a client is told.
        const auto* op = findOp (name);
        REQUIRE (op != nullptr);

        const auto schema = tool[juce::Identifier ("inputSchema")];
        REQUIRE (schema[juce::Identifier ("type")].toString() == "object");

        const auto properties = schema[juce::Identifier ("properties")];
        auto* object = properties.getDynamicObject();
        REQUIRE (object != nullptr);
        REQUIRE (object->getProperties().size() == (int) op->args.size());

        REQUIRE (tool[juce::Identifier ("annotations")][juce::Identifier ("readOnlyHint")]
                 == juce::var (op->scope == OpScope::read));
    }
}

TEST_CASE ("the parameter address space reaches every automatable target", "[control][gate]")
{
    // The two address spaces line up, and the statement that they do lives in
    // exactly one function. This walks every target the automation picker
    // offers, converts it to a parameter address, and asserts it resolves to
    // the SAME node the automation resolver gives - which is what stops
    // params_write and a right-clicked knob meaning two different parameters.
    testing::FakeHost host;
    const auto project = host.project();

    const auto targets = availableAutomationTargets (project);
    REQUIRE (targets.size() > 10);

    for (const auto& target : targets)
    {
        const auto address = addressOfTarget (target);

        INFO ("target: " << target.displayName << " -> " << address.describe());

        const auto viaAutomation = automationNodeFor (project, target.scope, target.targetId,
                                                      target.slot);
        const auto viaAddress = paramNodeFor (project, address);

        REQUIRE (viaAutomation.isValid());
        REQUIRE (viaAddress == viaAutomation);

        // And the address must find the parameter, not merely the node.
        const auto spec = paramSpecFor (project, address);
        REQUIRE (spec.has_value());
        REQUIRE (*spec->property == target.property);
    }
}

TEST_CASE ("the parameter address space reaches what automation deliberately cannot",
           "[control][gate]")
{
    // The reason this address space exists beside AutomationScope's. Every one
    // of these is a real parameter a person can set and a curve cannot drive,
    // and addressing "full control" through the automation tables would have
    // left all of them unreachable.
    testing::FakeHost host;
    const auto project = host.project();

    const auto reaches = [&project] (const juce::String& group, const juce::String& param)
    {
        ParamAddress address;
        address.target = "channel";
        address.id = 1;
        address.group = group;
        address.param = juce::Identifier (param);

        return paramSpecFor (project, address).has_value();
    };

    REQUIRE (reaches ("amp", "attack"));
    REQUIRE (reaches ("amp", "release"));

    // And what has no automation scope at all, which is the half of the claim
    // that makes the other half worth asserting.
    //
    // The envelope used to be the example here and is not any more: it has a
    // channelAmp scope now, so the two address spaces line up over it. What is
    // left is an insert on the MASTER, which the picker has never offered
    // either - there is no masterEffect scope and no third override struct in
    // the engine to write one into.
    ParamAddress masterInsert;
    masterInsert.target = "master";
    masterInsert.group = "effects";
    REQUIRE_FALSE (automationScopeOf (masterInsert).has_value());
}

TEST_CASE ("the guide routes to itself and to nothing missing", "[control][gate][mcp]")
{
    REQUIRE (guide().size() >= 5);
    REQUIRE (findGuideSection ("index") != nullptr);

    std::set<juce::String> ids;

    for (const auto& section : guide())
    {
        INFO ("guide section: " << section.id);

        REQUIRE (ids.insert (section.id).second);
        REQUIRE (juce::String (section.title).isNotEmpty());
        REQUIRE (juce::String (section.summary).isNotEmpty());
        REQUIRE (section.paragraphs.size() >= 3);

        REQUIRE (guideMarkdown (section).contains (section.title));
    }

    // Every dew://guide/ address any guide mentions must exist. A routing page
    // whose links are dead is worse than no routing page.
    for (const auto& section : guide())
    {
        for (const auto* paragraph : section.paragraphs)
        {
            juce::String text { paragraph };

            while (text.contains ("dew://guide/"))
            {
                text = text.fromFirstOccurrenceOf ("dew://guide/", false, false);
                const auto id = text.initialSectionContainingOnly (
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");

                INFO ("a guide links to dew://guide/" << id);
                REQUIRE (findGuideSection (id) != nullptr);
            }
        }
    }
}

TEST_CASE ("every guide section is one the protocol offers", "[control][gate][mcp]")
{
    const auto listed = mcp::resourcesList();
    const auto* resources = listed[juce::Identifier ("resources")].getArray();

    REQUIRE (resources != nullptr);
    REQUIRE ((std::size_t) resources->size() == guide().size());
}
