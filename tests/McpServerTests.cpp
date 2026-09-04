#include <catch2/catch_test_macros.hpp>

#include "ControlHarness.h"
#include "control/McpServer.h"

using namespace dew;
using namespace dew::control;
using namespace dew::testing;

namespace
{

/** Grants, in memory. Settings does this over a properties file. */
class FakeGrants : public McpServer::GrantStore
{
public:
    Grant grantFor (const juce::String& client) const override
    {
        const auto found = stored.find (client);
        return found == stored.end() ? Grant::none : found->second;
    }

    void setGrant (const juce::String& client, Grant grant) override
    {
        stored[client] = grant;
    }

    std::map<juce::String, Grant> stored;
};

/** The user, decided in advance.

    Answers inline, which a real dialog cannot - but the server's contract is
    that the reply may arrive at any time, and a test that always answered late
    would never exercise the fast path a remembered grant takes.
*/
class FakePrompt : public McpServer::ConsentPrompt
{
public:
    void ask (const McpServer::ClientInfo& client, std::function<void (Grant)> reply) override
    {
        ++timesAsked;
        lastClient = client;
        reply (answer);
    }

    Grant answer = Grant::readWrite;
    int timesAsked = 0;
    McpServer::ClientInfo lastClient;
};

struct Rig
{
    FakeHost host;
    FakeGrants grants;
    FakePrompt prompt;
    McpServer server { host, grants, prompt };

    LocalHttpServer::Request post (const juce::String& body, const juce::String& session = {})
    {
        LocalHttpServer::Request request;
        request.method = "POST";
        request.path = McpServer::endpoint;
        request.body = body;

        if (session.isNotEmpty())
            request.headers.set ("Mcp-Session-Id", session);

        return request;
    }
};

juce::String initializeBody (const juce::String& name = "Claude Code")
{
    return "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{"
           "\"name\":\""
           + name + "\",\"version\":\"1.0\"}}}";
}

} // namespace

TEST_CASE ("initialize is answered with a session the next request must carry", "[mcp][server]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;

    const auto opened = rig.server.handle (rig.post (initializeBody()));

    REQUIRE (opened.status == 200);

    const auto session = opened.extraHeaders["Mcp-Session-Id"];
    REQUIRE (session.isNotEmpty());

    // Visible ASCII only, as the transport requires of a session id.
    for (int i = 0; i < session.length(); ++i)
        REQUIRE ((session[i] > 0x20 && session[i] < 0x7f));

    // Unguessable, which is the only thing standing between a second local
    // process and an approval the user gave to the first.
    REQUIRE (session.length() >= 32);
    REQUIRE (rig.server.handle (rig.post (initializeBody())).extraHeaders["Mcp-Session-Id"]
             != session);

    const auto listed = rig.server.handle (
        rig.post ("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}", session));
    REQUIRE (listed.status == 200);
    REQUIRE (listed.body.contains ("project_describe"));
}

TEST_CASE ("a request with no session is told to start one", "[mcp][server]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;

    const auto orphan = rig.server.handle (
        rig.post ("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"));

    // 404 precisely: a client that gets one on a request carrying a session id
    // must start a new session. Any other status leaves it stuck.
    REQUIRE (orphan.status == 404);

    const auto stale = rig.server.handle (
        rig.post ("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", "notasession"));
    REQUIRE (stale.status == 404);
}

TEST_CASE ("a session ends when the client says so", "[mcp][server]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;

    const auto session = rig.server.handle (rig.post (initializeBody()))
                             .extraHeaders["Mcp-Session-Id"];

    LocalHttpServer::Request bye;
    bye.method = "DELETE";
    bye.path = McpServer::endpoint;
    bye.headers.set ("Mcp-Session-Id", session);

    REQUIRE (rig.server.handle (bye).status == 200);

    REQUIRE (
        rig.server.handle (rig.post ("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}", session))
            .status
        == 404);
}

TEST_CASE ("a browser origin is refused and a local one is not", "[mcp][server][consent]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;

    const auto withOrigin = [&rig] (const juce::String& origin)
    {
        auto request = rig.post (initializeBody());
        request.headers.set ("Origin", origin);

        return rig.server.handle (request).status;
    };

    // The attack this exists to stop: a page the user is merely visiting asks
    // their own machine to drive their DAW, and the browser attaches the site's
    // origin. Without this the loopback bind is not the protection it looks like.
    REQUIRE (withOrigin ("https://evil.example.com") == 403);
    REQUIRE (withOrigin ("http://dew.example.com:4551") == 403);
    REQUIRE (withOrigin ("file://") == 403);
    REQUIRE (withOrigin ("null") == 403);

    // A native client sends none at all, which is the ordinary case.
    REQUIRE (withOrigin ("http://localhost:3000") == 200);
    REQUIRE (withOrigin ("http://127.0.0.1:8080") == 200);
    REQUIRE (rig.server.handle (rig.post (initializeBody())).status == 200);
}

TEST_CASE ("a protocol version dew does not speak is refused", "[mcp][server]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;

    auto request = rig.post (initializeBody());
    request.headers.set ("MCP-Protocol-Version", "1999-01-01");

    // The specification says MUST. Being lenient sounds friendlier and is not:
    // a client sends the version it negotiated, so anything else means the two
    // ends disagree about what the messages mean.
    const auto refused = rig.server.handle (request);
    REQUIRE (refused.status == 400);
    REQUIRE (refused.body.contains (mcp::protocolVersion));

    request.headers.set ("MCP-Protocol-Version", mcp::protocolVersion);
    REQUIRE (rig.server.handle (request).status == 200);
}

TEST_CASE ("the user is asked once per client, and the answer is remembered",
           "[mcp][server][consent]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;

    const auto session = rig.server.handle (rig.post (initializeBody()))
                             .extraHeaders["Mcp-Session-Id"];

    REQUIRE (rig.prompt.timesAsked == 1);
    REQUIRE (rig.prompt.lastClient.name == "Claude Code");
    REQUIRE (rig.prompt.lastClient.version == "1.0");

    for (auto i = 0; i < 5; ++i)
        REQUIRE (
            rig.server
                .handle (rig.post ("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}", session))
                .status
            == 200);

    // Asked once. A prompt per request would make the feature unusable, which
    // is the reason the grant is stored against the client's name.
    REQUIRE (rig.prompt.timesAsked == 1);
    REQUIRE (rig.grants.grantFor ("Claude Code") == Grant::readWrite);

    // A different client is a different decision.
    rig.server.handle (rig.post (initializeBody ("Something Else")));
    REQUIRE (rig.prompt.timesAsked == 2);
}

TEST_CASE ("a denied client is refused and is not remembered as denied", "[mcp][server][consent]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    rig.prompt.answer = Grant::none;

    REQUIRE (rig.server.handle (rig.post (initializeBody())).status == 403);
    REQUIRE (rig.host.undoDepth() == 0);

    // Storing a refusal would mean a client the user later WANTS could never
    // ask again. So a denial is remembered as nothing, and asking again is how
    // they change their mind.
    REQUIRE (rig.grants.grantFor ("Claude Code") == Grant::none);

    rig.prompt.answer = Grant::read;
    REQUIRE (rig.server.handle (rig.post (initializeBody())).status == 200);
    REQUIRE (rig.grants.grantFor ("Claude Code") == Grant::read);
}

TEST_CASE ("a read-only approval reaches the tool call", "[mcp][server][consent]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    rig.prompt.answer = Grant::read;

    const auto session = rig.server.handle (rig.post (initializeBody()))
                             .extraHeaders["Mcp-Session-Id"];

    const auto refused = rig.server.handle (rig.post (
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"structure_"
        "write\",\"arguments\":{\"tempoBpm\":150}}}",
        session));

    REQUIRE (refused.status == 200);
    REQUIRE (refused.body.contains ("reading only"));

    // The whole chain, end to end: the user's choice reached the operation and
    // the document did not move.
    REQUIRE (rig.host.undoDepth() == 0);
}

TEST_CASE ("a notification is accepted with no body", "[mcp][server]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;

    const auto session = rig.server.handle (rig.post (initializeBody()))
                             .extraHeaders["Mcp-Session-Id"];

    const auto accepted = rig.server.handle (
        rig.post ("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", session));

    REQUIRE (accepted.status == 202);
    REQUIRE (accepted.body.isEmpty());
}

TEST_CASE ("a body that is not JSON is a parse error, not a crash", "[mcp][server]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;

    const auto refused = rig.server.handle (rig.post ("not json at all"));

    REQUIRE (refused.status == 400);
    REQUIRE (refused.body.contains ("-32700"));
}

TEST_CASE ("anything but the endpoint is not found, and GET is not offered", "[mcp][server]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;

    auto elsewhere = rig.post (initializeBody());
    elsewhere.path = "/";
    REQUIRE (rig.server.handle (elsewhere).status == 404);

    // 405 is what the transport says a server with no event stream answers GET
    // with, and dew has nothing to push: every answer is in a response body.
    LocalHttpServer::Request get;
    get.method = "GET";
    get.path = McpServer::endpoint;
    REQUIRE (rig.server.handle (get).status == 405);
}
