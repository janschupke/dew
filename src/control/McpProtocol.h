#pragma once

#include <optional>

#include "control/ControlOps.h"

namespace dew::control
{

/** What a client has been allowed to do.

    Three states rather than a boolean, because "not yet approved" is a
    different answer from "approved and may only read" and the two have to
    produce different HTTP. Deciding which of the three applies is the server's
    job; applying it is this file's, and the whole of that application is one
    comparison against OpSpec::scope.
*/
enum class Grant
{
    none,
    read,
    readWrite
};

juce::String grantToString (Grant);
Grant grantFromString (const juce::String&);

/** The protocol dew speaks, with nothing about sockets in it.

    JSON-RPC message in, JSON-RPC message out. It knows the operation table, the
    guide, and what a Grant permits, and it does not know what an HTTP request
    is - so the whole of the protocol is testable against a var built in a test,
    with no port, no thread and no client.

    ### Message thread only
    Every handler reaches the document through ControlHost. The server marshals
    each call from its socket thread and never calls this directly.
*/
namespace mcp
{

/** The revision this server implements.

    Pinned rather than echoed back. A client that asks for another version is
    answered with this one and decides for itself whether it can proceed, which
    is what the specification says to do - echoing whatever arrived would be
    claiming to speak a revision nobody here has read.
*/
inline constexpr const char* protocolVersion = "2025-06-18";

inline constexpr const char* serverName = "dew";

/** Every operation as an MCP tool, with a JSON Schema built from its ArgSpecs. */
juce::var toolsList();

/** One operation's input schema. Public so a test can hold it against the
    committed reference the website reads. */
juce::var schemaForArgs (const std::vector<ArgSpec>& args);

/** The guide, as MCP resources. */
juce::var resourcesList();

/** The answer to one JSON-RPC message.

    Returns nothing for a notification, which the transport answers with 202 and
    no body - the specification is explicit that a notification gets no JSON-RPC
    response, and returning an empty object instead is a real interoperability
    bug rather than a harmless one.
*/
std::optional<juce::var> dispatch (ControlHost&, const juce::var& message, Grant);

/** A JSON-RPC error object, for the transport to send when it cannot even get
    as far as a message - a body that is not JSON, or is not a request. */
juce::var errorResponse (const juce::var& id, int code, const juce::String& message);

// The codes JSON-RPC 2.0 defines. Written out rather than spelled at each call
// site, because -32602 and -32603 are one keystroke apart and mean quite
// different things to a client deciding whether to retry.
inline constexpr int parseError = -32700;
inline constexpr int invalidRequest = -32600;
inline constexpr int methodNotFound = -32601;
inline constexpr int invalidParams = -32602;
inline constexpr int internalError = -32603;

} // namespace mcp
} // namespace dew::control
