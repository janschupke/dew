// Before everything, and the one thing in this tree allowed to precede a file's
// own header: <cstdlib> declares rand_s only when this is defined, and rand_s is
// the Windows third of the generator newSessionId uses.
#if defined(_WIN32)
#define _CRT_RAND_S
#endif

#include "control/McpServer.h"
#include "control/MessageThreadCall.h"

#include <cstdlib>

#if defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#endif

namespace dew::control
{

namespace
{

constexpr const char* jsonType = "application/json";

/** The protocol revisions this server will answer to.

    The specification is explicit: an invalid or unsupported MCP-Protocol-Version
    MUST be answered with 400. Being lenient instead sounds friendlier and is
    not - a client sends the version it NEGOTIATED, which is the one initialize
    returned, so anything else means the two ends disagree about what the
    messages mean. 2025-03-26 is here because the specification names it as what
    to assume when the header is absent.
*/
bool isKnownProtocolVersion (const juce::String& version)
{
    return version == mcp::protocolVersion || version == "2025-03-26";
}

/** Whether a browser origin may drive this server.

    Absent is fine and is the ordinary case: a native client sends no Origin at
    all. A page does, and only a loopback one is allowed - which is what stops a
    site the user is merely VISITING from posting to their DAW, since the
    browser will attach the site's own origin and this refuses it.

    The `file://` and `null` origins are refused too. They look local and are
    what a downloaded HTML file gets.
*/
bool isAllowedOrigin (const juce::String& origin)
{
    if (origin.isEmpty())
        return true;

    const auto host = origin.fromFirstOccurrenceOf ("//", false, false)
                          .upToFirstOccurrenceOf ("/", false, false)
                          .upToLastOccurrenceOf (":", false, false);

    if (! origin.startsWithIgnoreCase ("http://") && ! origin.startsWithIgnoreCase ("https://"))
        return false;

    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

/** Fills a buffer from the system's cryptographically secure generator.

    Three implementations, because there is no portable one and a fallback would
    defeat the whole point: an id that is unguessable on macOS and predictable
    on Linux is a predictable id.

    It was arc4random_buf unconditionally, which is in libc on Apple and the
    BSDs and which glibc only grew in 2.36 - newer than the oldest distribution
    dew builds against. The Linux build did not compile at all.

      Apple, BSD   arc4random_buf   returns void, cannot fail
      Windows      rand_s           RtlGenRandom behind a CRT name, so this
                                    needs no bcrypt on the link line
      Linux        getrandom        glibc 2.25 and later; it may be interrupted
                                    or return short, so it is a loop

    False means the platform refused, which a caller must treat as "no id" and
    never as "an empty id".
*/
bool fillRandomBytes (unsigned char* out, size_t count)
{
#if JUCE_WINDOWS
    for (size_t i = 0; i < count; ++i)
    {
        unsigned int value = 0;

        if (rand_s (&value) != 0)
            return false;

        out[i] = (unsigned char) (value & 0xffu);
    }

    return true;
#elif JUCE_LINUX
    size_t filled = 0;

    while (filled < count)
    {
        const auto got = getrandom (out + filled, count - filled, 0);

        if (got < 0)
        {
            if (errno == EINTR)
                continue;

            return false;
        }

        filled += (size_t) got;
    }

    return true;
#else
    arc4random_buf (out, count);

    return true;
#endif
}

/** A session id, from that generator.

    NOT juce::Random, which is a pseudo-random generator for shuffling notes and
    is not unpredictable to anything that cares. A guessable session id is the
    one way past the consent prompt: a second local process could ride the
    approval the user gave to the first, and the user would never see a second
    dialog.

    Hex, so every character is in the visible ASCII range the transport requires
    of a session id. Empty means the generator failed, and is never a session.
*/
juce::String newSessionId()
{
    unsigned char bytes[16];

    if (! fillRandomBytes (bytes, sizeof (bytes)))
        return {};

    juce::String id;

    for (const auto byte : bytes)
        id += juce::String::toHexString ((int) byte).paddedLeft ('0', 2);

    return id;
}

LocalHttpServer::Response jsonResponse (int status, const juce::var& body)
{
    LocalHttpServer::Response response;
    response.status = status;
    response.contentType = jsonType;
    response.body = juce::JSON::toString (body, false);

    return response;
}

LocalHttpServer::Response protocolError (int status, int code, const juce::String& message)
{
    return jsonResponse (status, mcp::errorResponse ({}, code, message));
}

McpServer::ClientInfo clientFrom (const juce::var& message)
{
    const auto info = message[juce::Identifier ("params")][juce::Identifier ("clientInfo")];

    McpServer::ClientInfo client;
    client.name = info[juce::Identifier ("name")].toString();
    client.version = info[juce::Identifier ("version")].toString();

    if (client.name.isEmpty())
        client.name = "an unnamed client";

    return client;
}

} // namespace

McpServer::Dispatcher& McpServer::messageThread()
{
    class MessageThread : public Dispatcher
    {
    public:
        bool run (std::function<void()> work, int timeoutMs,
                  const std::atomic<bool>& givenUp) override
        {
            return callOnMessageThread (std::move (work), timeoutMs, givenUp);
        }
    };

    static MessageThread instance;
    return instance;
}

McpServer::McpServer (ControlHost& hostToUse, GrantStore& grantsToUse, ConsentPrompt& promptToUse,
                      Dispatcher& dispatcherToUse)
    : host (hostToUse)
    , grants (grantsToUse)
    , prompt (promptToUse)
    , dispatcher (dispatcherToUse)
{
}

McpServer::~McpServer()
{
    stop();
}

bool McpServer::start (int port)
{
    // Cleared here rather than only in the constructor, so a server that was
    // stopped and started again is not permanently refusing to wait.
    abandoned.store (false, std::memory_order_relaxed);

    return transport.start (port, [this] (const LocalHttpServer::Request& request)
                            { return handle (request); });
}

void McpServer::stop()
{
    // BEFORE the transport, and that order is the whole point. Stopping the
    // transport joins the socket thread with a two-second budget, and this runs
    // on the message thread - which is the thread a request in flight is
    // waiting on. Set first, the waiter gives up and unwinds; set after, or not
    // at all, the join times out and JUCE kills the thread mid-request.
    abandoned.store (true, std::memory_order_relaxed);

    transport.stop();

    const std::lock_guard<std::mutex> guard { lock };
    sessions.clear();
}

bool McpServer::isRunning() const
{
    return transport.isRunning();
}

int McpServer::getPort() const
{
    return transport.getPort();
}

juce::String McpServer::getUrl() const
{
    if (! isRunning())
        return {};

    return "http://127.0.0.1:" + juce::String (getPort()) + endpoint;
}

Grant McpServer::grantFor (const ClientInfo& client)
{
    // A decision the user has already made, taken without troubling them again.
    if (const auto stored = grants.grantFor (client.name); stored != Grant::none)
        return stored;

    // Otherwise ask. The dialog is raised on the message thread and answers
    // asynchronously - JUCE_MODAL_LOOPS_PERMITTED is 0 here, so it cannot be
    // driven to an answer inline - and this socket thread waits for it.
    struct Answer
    {
        juce::WaitableEvent given;
        std::atomic<Grant> grant { Grant::none };
    };

    auto answer = std::make_shared<Answer>();

    const auto asked = dispatcher.run (
        [this, client, answer]
        {
            prompt.ask (client,
                        [answer] (Grant decided)
                        {
                            answer->grant = decided;
                            answer->given.signal();
                        });
        },
        documentTimeoutMs, abandoned);

    // Sliced for the reason the dispatcher's wait is, and more urgently: three
    // minutes is a very long time to hold a socket thread that the message
    // thread may already be trying to join. An endpoint switched off while a
    // dialog is open answers "no" and lets go.
    const auto waitForAnswer = [this, &answer]
    {
        for (int waited = 0; waited < consentTimeoutMs; waited += consentSliceMs)
        {
            if (answer->given.wait (consentSliceMs))
                return true;

            if (abandoned.load (std::memory_order_relaxed))
                return false;
        }

        return false;
    };

    if (! asked || ! waitForAnswer())
        return Grant::none;

    const auto decided = answer->grant.load();

    // Remembered so the same client is not asked again. A denial is remembered
    // as nothing rather than as a "no": the user turned it down this time, and
    // storing a refusal would mean a client they later want could never ask.
    if (decided != Grant::none)
        dispatcher.run ([this, client, decided] { grants.setGrant (client.name, decided); },
                        documentTimeoutMs, abandoned);

    return decided;
}

juce::String McpServer::openSession (const ClientInfo& client)
{
    const auto id = newSessionId();

    // Storing an empty id would key a live session on the ABSENCE of a header,
    // which is exactly what a request carrying no session looks like. So a
    // generator that failed opens nothing, and the caller answers 500.
    if (id.isEmpty())
        return {};

    const std::lock_guard<std::mutex> guard { lock };
    sessions[id] = client;

    return id;
}

LocalHttpServer::Response McpServer::handle (const LocalHttpServer::Request& request)
{
    if (request.path != endpoint)
        return LocalHttpServer::Response::of (404, {});

    // Before anything else. A refused origin must not reach the session table,
    // let alone the document.
    if (! isAllowedOrigin (request.header ("Origin")))
        return protocolError (403, mcp::invalidRequest,
                              "This origin may not drive dew. The MCP endpoint is for local "
                              "clients.");

    // The transport says a server not offering an event stream answers GET with
    // 405, which is what this does: dew answers every request in its response
    // body and has nothing to push.
    if (request.method == "GET")
        return LocalHttpServer::Response::of (405, {});

    if (request.method == "DELETE")
    {
        const std::lock_guard<std::mutex> guard { lock };
        sessions.erase (request.header ("Mcp-Session-Id"));

        return LocalHttpServer::Response::of (200, {});
    }

    if (request.method != "POST")
        return LocalHttpServer::Response::of (405, {});

    if (const auto version = request.header ("MCP-Protocol-Version");
        version.isNotEmpty() && ! isKnownProtocolVersion (version))
        return protocolError (400, mcp::invalidRequest,
                              "dew speaks MCP " + juce::String (mcp::protocolVersion) + ", not "
                                  + version + ".");

    const auto message = juce::JSON::parse (request.body);

    if (message.getDynamicObject() == nullptr)
        return protocolError (400, mcp::parseError, "The body is not a JSON-RPC message.");

    const auto method = message[juce::Identifier ("method")].toString();
    const auto sessionId = request.header ("Mcp-Session-Id");

    ClientInfo client;

    if (method == "initialize")
    {
        client = clientFrom (message);
    }
    else
    {
        const std::lock_guard<std::mutex> guard { lock };
        const auto found = sessions.find (sessionId);

        // 404, and the specification is precise about why: a client that gets
        // one on a request carrying a session id must start a new session with
        // a fresh initialize. Answering 400 or 403 leaves it stuck.
        if (found == sessions.end())
            return protocolError (404, mcp::invalidRequest,
                                  "That session is not open. Send initialize again.");

        client = found->second;
    }

    // Asked once per client, not once per request - and by name, so the person
    // approving sees who is asking.
    const auto grant = grantFor (client);

    if (grant == Grant::none)
        return protocolError (403, mcp::invalidRequest,
                              client.name
                                  + " has not been allowed to control dew. There is a prompt in "
                                    "the application, or an entry in its MCP settings.");

    std::optional<juce::var> reply;

    // The document is the message thread's. Everything above this line is the
    // socket thread's, and nothing below it is.
    const auto ran = dispatcher.run ([this, &message, grant, &reply]
                                     { reply = mcp::dispatch (host, message, grant); },
                                     documentTimeoutMs, abandoned);

    if (! ran)
        return protocolError (503, mcp::internalError,
                              "dew did not answer in time. It may be busy or shutting down.");

    // A notification gets 202 and no body. Not an empty object: the
    // specification is explicit that a notification receives no JSON-RPC
    // response, and sending one is a real interoperability bug.
    if (! reply.has_value())
        return LocalHttpServer::Response::of (202, {});

    auto response = jsonResponse (200, *reply);

    if (method == "initialize")
    {
        const auto session = openSession (client);

        if (session.isEmpty())
            return protocolError (500, mcp::internalError,
                                  "dew could not mint a session id. Its system random number "
                                  "generator refused.");

        response.extraHeaders.set ("Mcp-Session-Id", session);
    }

    return response;
}

} // namespace dew::control
