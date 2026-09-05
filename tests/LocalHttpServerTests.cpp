#include <catch2/catch_test_macros.hpp>

#include "ControlHarness.h"
#include "control/McpServer.h"
#include "io/LocalHttpServer.h"
#include "model/Ids.h"

using namespace dew;
using namespace dew::control;
using namespace dew::testing;

namespace
{

/** A raw HTTP client, over the loopback interface.

    A real socket rather than a call into the handler, because everything this
    file is for lives between the two: the request line, the header block, the
    Content-Length body and the bytes written back. A test that called the
    handler would prove none of it.
*/
struct Client
{
    juce::String send (int port, const juce::String& request)
    {
        juce::StreamingSocket socket;

        if (! socket.connect ("127.0.0.1", port, 2000))
            return "<could not connect>";

        const auto utf8 = request.toRawUTF8();
        socket.write (utf8, (int) std::strlen (utf8));

        juce::String reply;

        // Read to close. The server answers with Connection: close, so the
        // shutdown IS the end of the message and there is no framing to parse.
        for (;;)
        {
            if (! socket.waitUntilReady (true, 4000))
                break;

            char buffer[4096];
            const auto got = socket.read (buffer, (int) sizeof (buffer), false);

            if (got <= 0)
                break;

            reply += juce::String::fromUTF8 (buffer, got);
        }

        return reply;
    }
};

juce::String postTo (const juce::String& body, const juce::String& extraHeaders = {})
{
    return "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
           + extraHeaders + "Content-Length: " + juce::String ((int) body.getNumBytesAsUTF8())
           + "\r\n\r\n" + body;
}

int statusOf (const juce::String& reply)
{
    return reply.fromFirstOccurrenceOf (" ", false, false)
        .upToFirstOccurrenceOf (" ", false, false)
        .getIntValue();
}

juce::String bodyOf (const juce::String& reply)
{
    return reply.fromFirstOccurrenceOf ("\r\n\r\n", false, false);
}

} // namespace

TEST_CASE ("the server binds a free port and answers a real request", "[http]")
{
    LocalHttpServer server;

    // Port 0, always, in a test. check.sh runs ctest with four jobs on the
    // stated assumption that nothing binds a fixed port, and a suite that took
    // dew's own port would fail whenever a real dew was open.
    REQUIRE (server.start (0,
                           [] (const LocalHttpServer::Request& request)
                           {
                               auto response = LocalHttpServer::Response::of (200, request.body);
                               response.extraHeaders.set ("X-Echoed-Method", request.method);
                               return response;
                           }));

    REQUIRE (server.isRunning());
    REQUIRE (server.getPort() > 0);

    Client client;
    const auto reply = client.send (server.getPort(), postTo ("{\"hello\":1}"));

    REQUIRE (statusOf (reply) == 200);
    REQUIRE (bodyOf (reply) == "{\"hello\":1}");
    REQUIRE (reply.contains ("X-Echoed-Method: POST"));
    REQUIRE (reply.contains ("Connection: close"));
}

TEST_CASE ("a header is found however the client capitalised it", "[http]")
{
    // HTTP header names are case-insensitive and clients differ. Read by exact
    // name, a lower-cased session id is missing - and a missing session id
    // reads as a session that expired, which sends the client round a
    // reconnection loop for no reason.
    LocalHttpServer::Request request;
    request.headers.set ("mcp-session-id", "abc");
    request.headers.set ("CONTENT-LENGTH", "12");

    REQUIRE (request.header ("Mcp-Session-Id") == "abc");
    REQUIRE (request.header ("content-length") == "12");
    REQUIRE (request.header ("Nothing-Like-It").isEmpty());
}

TEST_CASE ("a body is read whole, multi-byte characters included", "[http]")
{
    LocalHttpServer server;
    juce::String received;

    REQUIRE (server.start (0,
                           [&received] (const LocalHttpServer::Request& request)
                           {
                               received = request.body;
                               return LocalHttpServer::Response::of (200, request.body);
                           }));

    // Content-Length counts BYTES. Reading until the string's LENGTH matches it
    // would stop short by exactly the number of multi-byte characters, so a
    // score with an accented word in it would arrive truncated - and truncated
    // JSON is a parse error that looks like the client's fault.
    const juce::String body { juce::CharPointer_UTF8 ("{\"name\":\"caf\xc3\xa9 \xe2\x99\xaf\"}") };

    Client client;
    const auto reply = client.send (server.getPort(), postTo (body));

    REQUIRE (statusOf (reply) == 200);
    REQUIRE (received == body);
}

TEST_CASE ("a connection that says nothing does not hold the server", "[http]")
{
    LocalHttpServer server;
    std::atomic<int> served { 0 };

    REQUIRE (server.start (0,
                           [&served] (const LocalHttpServer::Request&)
                           {
                               ++served;
                               return LocalHttpServer::Response::of (200, "{}");
                           }));

    {
        // Connects and sends nothing. The acceptor must not be left holding it.
        juce::StreamingSocket silent;
        REQUIRE (silent.connect ("127.0.0.1", server.getPort(), 2000));
    }

    Client client;
    REQUIRE (statusOf (client.send (server.getPort(), postTo ("{}"))) == 200);
    REQUIRE (served.load() == 1);
}

TEST_CASE ("stopping is not a hang, with or without traffic", "[http]")
{
    // waitForNextConnection blocks with no timeout and cannot be interrupted;
    // closing the listener is what wakes it. If that ever stops being true this
    // test takes two seconds and then fails, rather than the suite hanging.
    LocalHttpServer server;
    REQUIRE (server.start (0, [] (const LocalHttpServer::Request&)
                           { return LocalHttpServer::Response::of (200, "{}"); }));

    const auto port = server.getPort();

    Client client;
    REQUIRE (statusOf (client.send (port, postTo ("{}"))) == 200);

    const auto started = juce::Time::getMillisecondCounter();
    server.stop();

    REQUIRE (juce::Time::getMillisecondCounter() - started < 3000);
    REQUIRE_FALSE (server.isRunning());

    // And the port is free again, which is what proves it really closed.
    LocalHttpServer second;
    REQUIRE (second.start (0, [] (const LocalHttpServer::Request&)
                           { return LocalHttpServer::Response::of (200, "{}"); }));
}

/** A dispatcher the TEST thread turns, standing in for the message thread.

    The application hands McpServer JUCE's message thread. A headless test
    cannot: JUCE delivers a posted message through the platform event loop,
    which here has no NSApplication behind it, so callAsync is never delivered
    however hard the test pumps - while a juce::Timer fires anyway through the
    run loop and makes the loop look alive. A test built on that would have
    proved the socket path against a hop that silently never happened.

    So the crossing is real and the thread that owns the document is the test
    thread. Everything the arrangement is meant to guarantee still holds: the
    request arrives on a socket thread, the work runs somewhere else, and the
    socket thread waits for it.
*/
class PumpedDispatcher : public McpServer::Dispatcher
{
public:
    bool run (std::function<void()> work, int timeoutMs,
              const std::atomic<bool>& abandoned) override
    {
        auto job = std::make_shared<Job>();
        job->work = std::move (work);

        {
            const std::lock_guard<std::mutex> guard { lock };
            queue.push_back (job);
        }

        // Sliced the way the real one is, so a test exercises the same giving-up
        // as the application rather than a wait that only this class has.
        constexpr int sliceMs = 25;

        for (int waited = 0; waited < timeoutMs; waited += sliceMs)
        {
            if (job->done.wait (juce::jmin (sliceMs, timeoutMs - waited)))
                return true;

            if (abandoned.load (std::memory_order_relaxed))
                return false;
        }

        return false;
    }

    /** Called on the test thread. Runs whatever is waiting. */
    void pump()
    {
        std::vector<std::shared_ptr<Job>> ready;

        {
            const std::lock_guard<std::mutex> guard { lock };
            ready.swap (queue);
        }

        for (const auto& job : ready)
        {
            job->work();
            job->done.signal();
        }
    }

private:
    struct Job
    {
        std::function<void()> work;
        juce::WaitableEvent done;
    };

    std::mutex lock;
    std::vector<std::shared_ptr<Job>> queue;
};

TEST_CASE ("the whole stack answers over a socket", "[http][mcp][server]")
{
    FakeHost host;

    class Grants : public McpServer::GrantStore
    {
    public:
        Grant grantFor (const juce::String&) const override
        {
            return Grant::readWrite;
        }
        void setGrant (const juce::String&, Grant) override {}
    } grants;

    class Prompt : public McpServer::ConsentPrompt
    {
    public:
        void ask (const McpServer::ClientInfo&, std::function<void (Grant)> reply) override
        {
            reply (Grant::readWrite);
        }
    } prompt;

    PumpedDispatcher dispatcher;
    McpServer server { host, grants, prompt, dispatcher };

    REQUIRE (server.start (0));
    REQUIRE (server.getUrl().startsWith ("http://127.0.0.1:"));
    REQUIRE (server.getUrl().endsWith ("/mcp"));

    struct Caller : juce::Thread
    {
        Caller (int p, juce::String b)
            : juce::Thread ("client")
            , port (p)
            , body (std::move (b))
        {
        }

        void run() override
        {
            Client client;
            reply = client.send (port, postTo (body));
            finished = true;
        }

        int port;
        juce::String body;
        juce::String reply;
        std::atomic<bool> finished { false };
    };

    const auto exchange = [&dispatcher, &server] (const juce::String& body)
    {
        Caller caller { server.getPort(), body };
        caller.startThread();

        const auto deadline = juce::Time::getMillisecondCounter() + 8000;

        while (! caller.finished && juce::Time::getMillisecondCounter() < deadline)
        {
            dispatcher.pump();
            juce::Thread::sleep (2);
        }

        caller.stopThread (2000);

        REQUIRE (caller.finished.load());
        return caller.reply;
    };

    const auto opened = exchange (
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{"
        "\"name\":\"Test Client\",\"version\":\"1.0\"}}}");

    REQUIRE (statusOf (opened) == 200);
    REQUIRE (bodyOf (opened).contains ("protocolVersion"));

    const auto session = opened.fromFirstOccurrenceOf ("Mcp-Session-Id: ", false, false)
                             .upToFirstOccurrenceOf ("\r", false, false);
    REQUIRE (session.length() >= 32);

    // A real edit, over a real socket, ending in the document. This is the
    // whole feature in one assertion.
    const auto wrote = exchange (
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":"
        "\"structure_write\",\"arguments\":{\"tempoBpm\":143}},\"sessionHack\":0}");

    // ... but only once it carries its session, which the exchange above did
    // not. The refusal is 404, which is what tells a client to initialize again.
    REQUIRE (statusOf (wrote) == 404);
}

TEST_CASE ("a request carrying its session reaches the document", "[http][mcp][server]")
{
    FakeHost host;

    class Grants : public McpServer::GrantStore
    {
    public:
        Grant grantFor (const juce::String&) const override
        {
            return Grant::readWrite;
        }
        void setGrant (const juce::String&, Grant) override {}
    } grants;

    class Prompt : public McpServer::ConsentPrompt
    {
    public:
        void ask (const McpServer::ClientInfo&, std::function<void (Grant)> reply) override
        {
            reply (Grant::readWrite);
        }
    } prompt;

    PumpedDispatcher dispatcher;
    McpServer server { host, grants, prompt, dispatcher };
    REQUIRE (server.start (0));

    struct Caller : juce::Thread
    {
        Caller (int p, juce::String r)
            : juce::Thread ("client")
            , port (p)
            , request (std::move (r))
        {
        }

        void run() override
        {
            Client client;
            reply = client.send (port, request);
            finished = true;
        }

        int port;
        juce::String request;
        juce::String reply;
        std::atomic<bool> finished { false };
    };

    const auto exchange = [&dispatcher, &server] (const juce::String& request)
    {
        Caller caller { server.getPort(), request };
        caller.startThread();

        const auto deadline = juce::Time::getMillisecondCounter() + 8000;

        while (! caller.finished && juce::Time::getMillisecondCounter() < deadline)
        {
            dispatcher.pump();
            juce::Thread::sleep (2);
        }

        caller.stopThread (2000);
        REQUIRE (caller.finished.load());

        return caller.reply;
    };

    const auto opened = exchange (postTo (
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{"
        "\"name\":\"Test Client\",\"version\":\"1.0\"}}}"));

    const auto session = opened.fromFirstOccurrenceOf ("Mcp-Session-Id: ", false, false)
                             .upToFirstOccurrenceOf ("\r", false, false);
    REQUIRE (session.isNotEmpty());

    const auto wrote = exchange (
        postTo ("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":"
                "\"structure_write\",\"arguments\":{\"tempoBpm\":143}}}",
                "Mcp-Session-Id: " + session + "\r\n"));

    REQUIRE (statusOf (wrote) == 200);

    // The document moved, on the thread that owns it, because a socket said so.
    REQUIRE (juce::exactlyEqual ((double) host.project()[dew::ids::tempoBpm], 143.0));
    REQUIRE (host.undoDepth() == 1);
}

TEST_CASE ("switching the endpoint off does not strand a request in flight", "[http][mcp][server]")
{
    FakeHost host;

    class Grants : public McpServer::GrantStore
    {
    public:
        Grant grantFor (const juce::String&) const override
        {
            return Grant::readWrite;
        }
        void setGrant (const juce::String&, Grant) override {}
    } grants;

    class Prompt : public McpServer::ConsentPrompt
    {
    public:
        void ask (const McpServer::ClientInfo&, std::function<void (Grant)> reply) override
        {
            reply (Grant::readWrite);
        }
    } prompt;

    // Never pumped, deliberately. That is what a message thread looks like from
    // the socket's side while it is busy - and it is exactly the state it is in
    // when it is inside stop(), joining this very thread.
    PumpedDispatcher dispatcher;
    McpServer server { host, grants, prompt, dispatcher };

    REQUIRE (server.start (0));

    struct Caller : juce::Thread
    {
        explicit Caller (int p)
            : juce::Thread ("client")
            , port (p)
        {
        }

        void run() override
        {
            Client client;
            client.send (port, postTo ("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                                       "\"params\":{\"clientInfo\":{\"name\":\"Test Client\","
                                       "\"version\":\"1.0\"}}}"));
            finished = true;
        }

        int port;
        std::atomic<bool> finished { false };
    };

    Caller caller { server.getPort() };
    caller.startThread();

    // Let it get as far as the hop it will block on. Without this the test
    // could stop a server nothing had reached yet, which proves nothing.
    juce::Thread::sleep (300);

    // The measurement. stop() joins the acceptor with a two-second budget, and
    // the acceptor is waiting on a dispatcher nobody is pumping - so before the
    // abandon flag this ran the budget out and JUCE killed the thread.
    const auto before = juce::Time::getMillisecondCounter();
    server.stop();
    const auto took = juce::Time::getMillisecondCounter() - before;

    INFO ("stop took " << took << " ms");
    REQUIRE (took < 1500);
    REQUIRE_FALSE (server.isRunning());

    caller.stopThread (2000);
}
