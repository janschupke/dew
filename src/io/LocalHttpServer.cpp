#include "io/LocalHttpServer.h"

namespace dew
{

namespace
{

constexpr const char* loopback = "127.0.0.1";

/** The reason phrase, for the statuses this server actually sends.

    Written out rather than derived, and short by design: a server that can
    answer with any status is a server somebody will use to answer with any
    status. Anything unlisted is reported as the class it belongs to, which is
    what HTTP requires of a client anyway.
*/
const char* reasonFor (int status)
{
    switch (status)
    {
        case 200: return "OK";
        case 202: return "Accepted";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: break;
    }

    return status < 400 ? "OK" : "Error";
}

/** Whether a connected peer is on this machine.

    The listener already binds loopback, so this cannot fail in ordinary
    operation - which is exactly why it is here. A bind is a claim about what
    the OS was asked for; this is the proof about what actually arrived, and it
    costs one string comparison per connection.
*/
bool isLoopback (const juce::String& host)
{
    return host == loopback || host == "::1" || host == "::ffff:127.0.0.1" || host == "localhost";
}

} // namespace

juce::String LocalHttpServer::Request::header (juce::StringRef name) const
{
    // HTTP header names are case-insensitive, and clients differ: a
    // StringPairArray looked up by exact name silently misses `mcp-session-id`
    // from a client that lower-cases everything, and a missing session id reads
    // as a session that expired.
    for (int i = 0; i < headers.size(); ++i)
        if (headers.getAllKeys()[i].equalsIgnoreCase (name))
            return headers.getAllValues()[i];

    return {};
}

LocalHttpServer::Response LocalHttpServer::Response::of (int status, const juce::String& body)
{
    Response response;
    response.status = status;
    response.body = body;

    return response;
}

/** The listener, and the loop that answers on it. */
class LocalHttpServer::Acceptor : public juce::Thread
{
public:
    Acceptor (int port, Handler handlerToUse)
        : juce::Thread ("dew MCP")
        , handler (std::move (handlerToUse))
    {
        listening = listener.createListener (port, loopback);
    }

    ~Acceptor() override
    {
        signalThreadShouldExit();

        // Closing the listener is what wakes waitForNextConnection, which
        // otherwise blocks with no timeout and no way to be interrupted. Order
        // matters: signal first, then close, or the loop can accept once more
        // between the two and answer a request during shutdown.
        listener.close();

        stopThread (2000);
    }

    bool isListening() const noexcept
    {
        return listening;
    }

    int port() const noexcept
    {
        return listener.getBoundPort();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> connection { listener.waitForNextConnection() };

            if (connection == nullptr)
                continue;

            if (threadShouldExit())
                break;

            serve (*connection);
        }
    }

private:
    /** Reads until the header terminator, then the declared body. */
    bool readRequest (juce::StreamingSocket& socket, Request& request)
    {
        juce::String text;

        while (! text.contains ("\r\n\r\n"))
        {
            if (threadShouldExit() || ! socket.waitUntilReady (true, readTimeoutMs))
                return false;

            char buffer[2048];
            const auto got = socket.read (buffer, (int) sizeof (buffer), false);

            if (got <= 0)
                return false;

            text += juce::String::fromUTF8 (buffer, got);

            if (text.length() > maximumBodyBytes)
                return false;
        }

        const auto headerText = text.upToFirstOccurrenceOf ("\r\n\r\n", false, false);
        auto body = text.fromFirstOccurrenceOf ("\r\n\r\n", false, false);

        juce::StringArray lines;
        lines.addLines (headerText);

        if (lines.isEmpty())
            return false;

        juce::StringArray requestLine;
        requestLine.addTokens (lines[0], " ", "");

        if (requestLine.size() < 2)
            return false;

        request.method = requestLine[0];
        request.path = requestLine[1].upToFirstOccurrenceOf ("?", false, false);

        for (int i = 1; i < lines.size(); ++i)
        {
            const auto name = lines[i].upToFirstOccurrenceOf (":", false, false).trim();

            if (name.isNotEmpty())
                request.headers.set (name,
                                     lines[i].fromFirstOccurrenceOf (":", false, false).trim());
        }

        const auto declared = request.header ("Content-Length").getIntValue();

        if (declared > maximumBodyBytes)
            return false;

        // Read the rest of the body. `getNumBytesAsUTF8` rather than length():
        // the count in the header is BYTES, and a score with an accented word
        // in it would otherwise be read short by exactly the number of
        // multi-byte characters in it.
        while (body.getNumBytesAsUTF8() < (size_t) declared)
        {
            if (threadShouldExit() || ! socket.waitUntilReady (true, readTimeoutMs))
                return false;

            char buffer[4096];
            const auto got = socket.read (buffer, (int) sizeof (buffer), false);

            if (got <= 0)
                return false;

            body += juce::String::fromUTF8 (buffer, got);
        }

        request.body = body;

        return true;
    }

    void send (juce::StreamingSocket& socket, const Response& response)
    {
        juce::String head;
        head << "HTTP/1.1 " << response.status << " " << reasonFor (response.status) << "\r\n";

        if (response.body.isNotEmpty())
            head << "Content-Type: " << response.contentType << "; charset=utf-8\r\n";

        head << "Content-Length: " << (int) response.body.getNumBytesAsUTF8() << "\r\n";

        for (int i = 0; i < response.extraHeaders.size(); ++i)
            head << response.extraHeaders.getAllKeys()[i] << ": "
                 << response.extraHeaders.getAllValues()[i] << "\r\n";

        // Answered and closed. Keep-alive would leave this thread holding a
        // connection that may send nothing more, and there is nothing to gain:
        // the work behind every request is serialised on the message thread.
        head << "Connection: close\r\n\r\n";

        const auto text = head + response.body;
        const auto utf8 = text.toRawUTF8();

        socket.write (utf8, (int) std::strlen (utf8));
    }

    void serve (juce::StreamingSocket& socket)
    {
        // The listener binds loopback, so this holds already. Checked anyway:
        // the bind is what was asked for, and this is what arrived.
        if (! isLoopback (socket.getHostName()))
        {
            send (socket, Response::of (403, {}));
            socket.close();
            return;
        }

        Request request;

        if (! readRequest (socket, request))
        {
            socket.close();
            return;
        }

        send (socket, handler (request));
        socket.close();
    }

    juce::StreamingSocket listener;
    Handler handler;
    bool listening = false;
};

LocalHttpServer::LocalHttpServer() = default;

LocalHttpServer::~LocalHttpServer()
{
    stop();
}

bool LocalHttpServer::start (int port, Handler handler)
{
    stop();

    auto candidate = std::make_unique<Acceptor> (port, std::move (handler));

    if (! candidate->isListening())
        return false;

    candidate->startThread();
    acceptor = std::move (candidate);

    return true;
}

void LocalHttpServer::stop()
{
    acceptor.reset();
}

bool LocalHttpServer::isRunning() const noexcept
{
    return acceptor != nullptr;
}

int LocalHttpServer::getPort() const noexcept
{
    return acceptor != nullptr ? acceptor->port() : 0;
}

} // namespace dew
