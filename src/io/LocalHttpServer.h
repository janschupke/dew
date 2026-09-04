#pragma once

#include <functional>

#include <juce_core/juce_core.h>

namespace dew
{

/** An HTTP/1.1 server bound to the loopback interface, and nothing more.

    It knows nothing about MCP, JSON-RPC or dew's document. A request comes in
    as text, a handler answers, and the answer goes out - which is what lets the
    protocol above it be tested with no socket, and this be tested with no
    protocol.

    ### Why hand-written

    dew has two dependencies and adding a third is three edits and a licence row
    in a generated manifest. juce_core already ships StreamingSocket, and the
    subset of HTTP a local MCP endpoint needs is a request line, a handful of
    headers and a Content-Length body. A general HTTP server would be the wrong
    thing to take on: this one refuses everything it does not understand.

    ### Loopback, and only loopback

    The listener binds 127.0.0.1, so nothing off the machine can reach it at
    all, and every connection's peer is checked as well - a bind is a claim and
    the check is the proof. The MCP specification requires both this and the
    Origin validation the handler above performs, because without them a page in
    the user's browser could drive their DAW.

    ### One connection at a time

    Handled on the acceptor thread, serially, with `Connection: close`. That is
    not a limitation being tolerated: every request has to be marshalled onto
    the message thread to touch the document, and the message thread is one
    thread - so a pool would queue in a different place and answer no sooner. A
    client that connects and then says nothing is dropped after
    `readTimeoutMs` rather than holding the door.
*/
class LocalHttpServer
{
public:
    struct Request
    {
        juce::String method; ///< POST, GET, DELETE
        juce::String path;   ///< "/mcp"
        juce::String body;

        /** Case-insensitively, because HTTP header names are, and a client that
            sends `mcp-session-id` is as correct as one that sends
            `Mcp-Session-Id`. Reading them out of a StringPairArray by exact
            name is the bug this exists to prevent. */
        juce::String header (juce::StringRef name) const;

        juce::StringPairArray headers;
    };

    struct Response
    {
        int status = 200;
        juce::String contentType = "application/json";
        juce::String body;

        /** Anything else the protocol needs on the way out, such as a session
            id. Written verbatim. */
        juce::StringPairArray extraHeaders;

        static Response of (int status, const juce::String& body);
    };

    /** Called on the SERVER'S thread, never the message thread. Whatever it
        does about that is its own business - which for dew means marshalling,
        and is why this header knows nothing about it. */
    using Handler = std::function<Response (const Request&)>;

    LocalHttpServer();
    ~LocalHttpServer();

    /** Binds 127.0.0.1 on `port`, or any free port when that is 0.

        Port 0 is what every test uses: `check.sh` runs ctest with four jobs on
        the stated assumption that nothing binds a fixed port or writes a shared
        path, and a suite that took 4551 would fail whenever a real dew was
        open.
    */
    bool start (int port, Handler);

    void stop();

    bool isRunning() const noexcept;

    /** The port actually bound, asked of the socket rather than remembered -
        which is the whole point when the caller asked for 0. */
    int getPort() const noexcept;

    /** How long a connected client has to send its request line before it is
        dropped. Short, because a stalled client holds the acceptor. */
    static constexpr int readTimeoutMs = 5000;

    /** The largest body this will read. An MCP request is a JSON-RPC message;
        two megabytes is a very large score and far short of anything that could
        exhaust memory. Refused with 413 rather than read. */
    static constexpr int maximumBodyBytes = 2 * 1024 * 1024;

private:
    class Acceptor;

    std::unique_ptr<Acceptor> acceptor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LocalHttpServer)
};

} // namespace dew
