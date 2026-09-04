#include <catch2/catch_test_macros.hpp>

#include "ControlHarness.h"
#include "control/McpProtocol.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"

using namespace dew;
using namespace dew::control;
using namespace dew::testing;

namespace
{

juce::var request (const juce::String& method, const juce::var& params = {}, int id = 1)
{
    return Fields {}
        .with ("jsonrpc", "2.0")
        .with ("id", id)
        .with ("method", method)
        .with ("params", params);
}

juce::var toolCall (const juce::String& name, const juce::var& args = {})
{
    return request ("tools/call", Fields {}.with ("name", name).with ("arguments", args));
}

/** The text a tool result carries, which is the only part every client reads. */
juce::String textOf (const juce::var& reply)
{
    const auto content = reply[juce::Identifier ("result")][juce::Identifier ("content")];
    const auto* items = content.getArray();

    if (items == nullptr || items->isEmpty())
        return {};

    return items->getReference (0)[juce::Identifier ("text")].toString();
}

bool isToolError (const juce::var& reply)
{
    return (bool) reply[juce::Identifier ("result")][juce::Identifier ("isError")];
}

} // namespace

TEST_CASE ("initialize answers with the version dew implements", "[mcp]")
{
    FakeHost host;

    const auto reply = mcp::dispatch (host, request ("initialize"), Grant::readWrite);
    REQUIRE (reply.has_value());

    const auto result = (*reply)[juce::Identifier ("result")];

    // Pinned, never echoed. Answering with whatever the client asked for would
    // be claiming to speak a revision nobody here has read.
    REQUIRE (result[juce::Identifier ("protocolVersion")].toString()
             == juce::String (mcp::protocolVersion));
    REQUIRE (result[juce::Identifier ("serverInfo")][juce::Identifier ("name")].toString()
             == "dew");
    REQUIRE (result[juce::Identifier ("capabilities")].hasProperty (juce::Identifier ("tools")));
    REQUIRE (
        result[juce::Identifier ("capabilities")].hasProperty (juce::Identifier ("resources")));

    // The instructions are what a client reads before its first call, so they
    // must point at the routing resource rather than being decoration.
    REQUIRE (result[juce::Identifier ("instructions")].toString().contains ("dew://guide/index"));
}

TEST_CASE ("a notification gets no response at all", "[mcp]")
{
    FakeHost host;

    // No id, so no reply - and the transport turns that into 202 with an empty
    // body. Answering an empty object instead is a real interoperability bug.
    auto notification = Fields {}
                            .with ("jsonrpc", "2.0")
                            .with ("method", "notifications/initialized")
                            .operator juce::var ();

    REQUIRE_FALSE (mcp::dispatch (host, notification, Grant::readWrite).has_value());
}

TEST_CASE ("an unimplemented method is a JSON-RPC error, not a tool error", "[mcp]")
{
    FakeHost host;

    const auto reply = mcp::dispatch (host, request ("prompts/list"), Grant::readWrite);
    REQUIRE (reply.has_value());

    const auto error = (*reply)[juce::Identifier ("error")];
    REQUIRE ((int) error[juce::Identifier ("code")] == mcp::methodNotFound);
}

TEST_CASE ("a tool that refuses is a result, not a JSON-RPC error", "[mcp]")
{
    // The specification's own distinction. A JSON-RPC error means the request
    // could not be processed; a tool that ran and refused has a sentence the
    // model should read and act on, and sending it as an error hides that.
    FakeHost host;

    const auto reply = mcp::dispatch (host, toolCall ("no_such_tool"), Grant::readWrite);
    REQUIRE (reply.has_value());

    REQUIRE_FALSE ((*reply).hasProperty (juce::Identifier ("error")));
    REQUIRE (isToolError (*reply));
    REQUIRE (textOf (*reply).contains ("no_such_tool"));
}

TEST_CASE ("a read-only grant refuses every writing tool and no reading one", "[mcp][consent]")
{
    FakeHost host;
    const auto pattern = ProjectEdits::findPattern (host.project(), 1);

    const auto write = toolCall (
        "notes_write",
        Fields {}
            .with ("patternId", 1)
            .with (
                "notes",
                list ({ Fields {}.with ("channelId", 1).with ("step", 300).with ("pitch", 60) })));

    const auto refused = mcp::dispatch (host, write, Grant::read);
    REQUIRE (refused.has_value());
    REQUIRE (isToolError (*refused));
    REQUIRE (textOf (*refused).contains ("reading only"));

    // Refused BEFORE it ran, which is the only refusal worth having.
    REQUIRE (host.undoDepth() == 0);

    // And a read is not refused.
    const auto allowed = mcp::dispatch (host, toolCall ("project_describe"), Grant::read);
    REQUIRE (allowed.has_value());
    REQUIRE_FALSE (isToolError (*allowed));

    // The same call under a full grant goes through, which is what proves the
    // refusal above was the grant and not something else.
    const auto done = mcp::dispatch (host, write, Grant::readWrite);
    REQUIRE (done.has_value());
    REQUIRE_FALSE (isToolError (*done));
    REQUIRE (host.undoDepth() == 1);
}

TEST_CASE ("every writing tool is refused under a read-only grant", "[mcp][consent]")
{
    // The whole table, not one example. A single mis-scoped operation is a
    // client writing to a project it was only allowed to read, and that is the
    // one failure this feature must not have.
    FakeHost host;

    auto writes = 0;

    for (const auto& op : ops())
    {
        if (op.scope != OpScope::write)
            continue;

        ++writes;

        const auto reply = mcp::dispatch (host, toolCall (op.name), Grant::read);

        INFO ("operation: " << op.name);
        REQUIRE (reply.has_value());
        REQUIRE (isToolError (*reply));
        REQUIRE (textOf (*reply).contains ("reading only"));
    }

    // The control case: a loop over an empty list proves nothing.
    REQUIRE (writes > 15);
    REQUIRE (host.undoDepth() == 0);
}

TEST_CASE ("a grant is never read back more permissively than it was written", "[mcp][consent]")
{
    REQUIRE (grantFromString (grantToString (Grant::none)) == Grant::none);
    REQUIRE (grantFromString (grantToString (Grant::read)) == Grant::read);
    REQUIRE (grantFromString (grantToString (Grant::readWrite)) == Grant::readWrite);

    // A settings file written by a later dew, or corrupted, must not become a
    // permissive grant. Anything unrecognised is none.
    REQUIRE (grantFromString ("") == Grant::none);
    REQUIRE (grantFromString ("readwrite") == Grant::none);
    REQUIRE (grantFromString ("admin") == Grant::none);
}

TEST_CASE ("bad arguments are reported to the model rather than thrown away", "[mcp]")
{
    FakeHost host;

    const auto reply = mcp::dispatch (
        host, toolCall ("structure_write", Fields {}.with ("tempoBmp", 120)), Grant::readWrite);

    REQUIRE (reply.has_value());
    REQUIRE (isToolError (*reply));
    REQUIRE (textOf (*reply).contains ("tempoBmp"));
}

TEST_CASE ("a tool answers as text and as structured content", "[mcp]")
{
    FakeHost host;

    const auto reply = mcp::dispatch (host, toolCall ("project_describe"), Grant::read);
    REQUIRE (reply.has_value());

    const auto result = (*reply)[juce::Identifier ("result")];

    // Every client reads text; not every client reads structuredContent yet, so
    // a tool whose answer is only in the second is invisible to half of them.
    REQUIRE (textOf (*reply).isNotEmpty());
    REQUIRE (
        result[juce::Identifier ("structuredContent")].hasProperty (juce::Identifier ("tempoBpm")));
}

TEST_CASE ("the guide is listed and readable, and a missing one is an error", "[mcp]")
{
    FakeHost host;

    const auto listed = mcp::dispatch (host, request ("resources/list"), Grant::read);
    REQUIRE (listed.has_value());

    const auto* resources = (*listed)[juce::Identifier ("result")][juce::Identifier ("resources")]
                                .getArray();
    REQUIRE (resources != nullptr);
    REQUIRE (resources->size() >= 5);

    const auto read = mcp::dispatch (
        host, request ("resources/read", Fields {}.with ("uri", "dew://guide/index")), Grant::read);
    REQUIRE (read.has_value());

    const auto contents = (*read)[juce::Identifier ("result")][juce::Identifier ("contents")]
                              .getArray()
                              ->getReference (0);
    REQUIRE (contents[juce::Identifier ("text")].toString().contains ("project_describe"));

    const auto missing = mcp::dispatch (
        host, request ("resources/read", Fields {}.with ("uri", "dew://guide/nope")), Grant::read);
    REQUIRE (missing.has_value());
    REQUIRE ((int) (*missing)[juce::Identifier ("error")][juce::Identifier ("code")]
             == mcp::invalidParams);
}
