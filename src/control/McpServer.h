#pragma once

#include <map>
#include <mutex>

#include "control/ControlHost.h"
#include "control/McpProtocol.h"
#include "io/LocalHttpServer.h"

namespace dew::control
{

/** dew's MCP endpoint: a loopback HTTP server, sessions, and the consent rule.

    Three collaborators, each of which lives above this library and arrives as an
    interface, so the whole server can be driven in a test with no window and no
    user: a ControlHost for the document, a GrantStore for what the user has
    already decided, and a ConsentPrompt for asking them when they have not.

    ### What protects the user

    Four things, and none of them is a password.

    - The listener binds 127.0.0.1, so nothing off the machine can reach it.
    - The Origin header is validated, so a page in the user's browser cannot
      drive their DAW by asking their own machine to. The specification requires
      this, and without it the loopback bind is not the protection it looks
      like.
    - A client is NAMED to the user and approved by them, once, choosing whether
      it may write. The name comes from the client's own initialize, so it is a
      claim rather than a proof - which is exactly why the person at the
      keyboard is the one who decides.
    - Every change is one undo step.

    ### Sessions

    A session id is minted at initialize and required on everything after it, as
    the transport specifies. It is generated with the system's CSPRNG rather
    than juce::Random, which is not one: a guessable id would let a second local
    process ride an approval the user gave to the first, which is the only way
    around the consent prompt there is.
*/
class McpServer
{
public:
    /** What a client says it is. A claim, not a proof - see above. */
    struct ClientInfo
    {
        juce::String name;
        juce::String version;
    };

    /** Where a decision the user has already made is remembered.

        Implemented over Settings, which is dew_app's and therefore above this
        library. An interface rather than a direct call for the reason
        ControlHost is one: the server has to be testable without a properties
        file on disk.
    */
    class GrantStore
    {
    public:
        virtual ~GrantStore() = default;

        virtual Grant grantFor (const juce::String& clientName) const = 0;
        virtual void setGrant (const juce::String& clientName, Grant) = 0;
    };

    /** Asks the person at the keyboard. Implemented in dew_ui, which is the
        only layer that may put something on screen.

        Called ON THE MESSAGE THREAD and answers asynchronously, because
        JUCE_MODAL_LOOPS_PERMITTED is 0 here: a dialog cannot be driven to an
        answer inline, and the socket thread waits on the callback instead.
    */
    class ConsentPrompt
    {
    public:
        virtual ~ConsentPrompt() = default;

        virtual void ask (const ClientInfo&, std::function<void (Grant)> reply) = 0;
    };

    /** How a request reaches the thread that owns the document.

        The document is a ValueTree and is not thread safe, so everything a
        request does has to cross from the socket thread to the thread that
        owns it. In the application that is JUCE's message thread and this is
        `messageThread()` below.

        It is an interface rather than a call to callAsync because the crossing
        is a POLICY, and because of a hard fact about this environment: JUCE
        delivers a posted message through the platform's event loop, which in a
        headless test has no NSApplication behind it - so callAsync is never
        delivered however hard a test pumps, while a juce::Timer fires anyway
        through the run loop and makes it look as though the loop is working.
        A test that could not substitute this could not exercise the socket path
        at all, and would have proved the transport against a hop that silently
        never happened.
    */
    class Dispatcher
    {
    public:
        virtual ~Dispatcher() = default;

        /** Runs `work` on the document's thread and waits for it.
            @returns false if it did not get to it in time. */
        virtual bool run (std::function<void()> work, int timeoutMs) = 0;
    };

    /** JUCE's message thread. What the application uses. */
    static Dispatcher& messageThread();

    McpServer (ControlHost&, GrantStore&, ConsentPrompt&, Dispatcher& = messageThread());
    ~McpServer();

    /** Binds the port and starts listening. Port 0 takes any free one, which is
        what every test uses. */
    bool start (int port);
    void stop();

    bool isRunning() const;
    int getPort() const;

    /** The address to give a client, or empty when it is not running. */
    juce::String getUrl() const;

    /** The endpoint path. One path for POST, GET and DELETE, as the transport
        requires. */
    static constexpr const char* endpoint = "/mcp";

    /** The port dew asks for by default.

        A fixed port so `claude mcp add` need be run once rather than after every
        launch, and the next free one when it is taken. Registered to nothing;
        chosen high and out of the way.
    */
    static constexpr int defaultPort = 4551;

    /** How long a request waits for the message thread. Generous: a document
        operation is fast, but it queues behind whatever the interface is
        already doing. */
    static constexpr int documentTimeoutMs = 20000;

    /** How long a request waits for a PERSON to answer the consent dialog.
        Long, because they may be looking at something else; bounded, because a
        socket thread cannot wait forever. */
    static constexpr int consentTimeoutMs = 180000;

    /** Answers one HTTP request. Public so a test can drive the whole protocol
        - Origin rules, sessions, consent and all - with no socket at all. */
    LocalHttpServer::Response handle (const LocalHttpServer::Request&);

private:
    Grant grantFor (const ClientInfo&);
    juce::String openSession (const ClientInfo&);

    ControlHost& host;
    GrantStore& grants;
    ConsentPrompt& prompt;
    Dispatcher& dispatcher;

    LocalHttpServer transport;

    /** Session id -> the client it belongs to. Touched from the socket thread
        and from nothing else, but guarded anyway: `stop` can arrive from the
        message thread while a request is in flight. */
    mutable std::mutex lock;
    std::map<juce::String, ClientInfo> sessions;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (McpServer)
};

} // namespace dew::control
