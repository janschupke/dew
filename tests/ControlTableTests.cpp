#include <set>

#include <catch2/catch_test_macros.hpp>

#include "ControlHarness.h"
#include "control/ControlGuide.h"
#include "control/McpProtocol.h"
#include "control/ParamAddress.h"

using namespace dew;
using namespace dew::control;

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

TEST_CASE ("a write operation is one that writes, and a read one does not", "[control][gate]")
{
    // OpScope is the ONLY permission gate, so a mis-scoped operation is a
    // client writing under a read-only grant. Named rather than inferred:
    // the point is that somebody has to say so per operation.
    for (const auto& op : ops())
    {
        const juce::String name { op.name };
        const auto looksLikeAWrite = name.contains ("_write") || name.contains ("_remove")
                                     || name.contains ("_move") || name.contains ("_compile")
                                     || name.contains ("_transform") || name.contains ("_command")
                                     || name.startsWith ("render_") || name.startsWith ("export_");

        INFO ("operation: " << op.name);

        if (op.scope == OpScope::write)
            REQUIRE (looksLikeAWrite);
        else
            REQUIRE_FALSE (name.contains ("_write"));
    }

    // render_status is the exception the rule above has to allow: it starts
    // with render_ and only reads. Asserted rather than left implied, so the
    // predicate cannot quietly stop covering the rest.
    REQUIRE (findOp ("render_status")->scope == OpScope::read);
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

    // And each of those has no automation scope at all, which is the half of
    // the claim that makes the other half worth asserting.
    ParamAddress amp;
    amp.target = "channel";
    amp.group = "amp";
    REQUIRE_FALSE (automationScopeOf (amp).has_value());
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
