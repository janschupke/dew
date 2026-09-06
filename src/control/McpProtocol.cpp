#include "control/ControlGuide.h"
#include "control/ControlValue.h"
#include "control/McpProtocol.h"

namespace dew::control
{

juce::String grantToString (Grant grant)
{
    // A switch with no default: -Wswitch-enum is an error under the ci preset,
    // so a grant added to the enum fails to compile until it has a spelling -
    // and a grant with no spelling is one that would be persisted as the empty
    // string and read back as `none`.
    switch (grant)
    {
        case Grant::none: return "none";
        case Grant::read: return "read";
        case Grant::readWrite: return "readWrite";
    }

    return "none";
}

Grant grantFromString (const juce::String& text)
{
    if (text == "readWrite")
        return Grant::readWrite;

    if (text == "read")
        return Grant::read;

    // Anything unrecognised is NO grant. A stored grant that this build cannot
    // read must not fall back to a permissive one: a settings file written by a
    // later dew, or corrupted, would otherwise silently authorise a client
    // nobody approved.
    return Grant::none;
}

namespace mcp
{

namespace
{

/** A juce::var object, built a member at a time. Local to this file rather than
    shared with OpsSupport.h, which is the operations' own header and has no
    business being included by the protocol. */
struct Obj
{
    Obj()
        : object (new juce::DynamicObject())
    {
    }

    Obj& set (const juce::Identifier& key, const juce::var& value)
    {
        object->setProperty (key, value);
        return *this;
    }

    operator juce::var () const // NOLINT(google-explicit-constructor)
    {
        return juce::var (object.get());
    }

    juce::DynamicObject::Ptr object;
};

/** A ValueKind as JSON Schema spells its type.

    A switch with no default, for the reason every other one in this tree has
    none: a kind added to ControlTypes.h fails to compile here until somebody
    says how a client should be told about it. `any` deliberately has no type -
    a parameter's value is whatever that parameter takes, and constraining it
    would make every choice unreachable.
*/
const char* jsonTypeOf (ValueKind kind)
{
    switch (kind)
    {
        case ValueKind::text: return "string";
        case ValueKind::integer: return "integer";
        case ValueKind::number: return "number";
        case ValueKind::flag: return "boolean";
        case ValueKind::object: return "object";
        case ValueKind::array: return "array";
        case ValueKind::any: return "";
    }

    return "";
}

juce::var schemaForOne (const ArgSpec& spec);

juce::var objectSchema (const std::vector<ArgSpec>& fields)
{
    Obj properties;
    juce::Array<juce::var> required;

    for (const auto& field : fields)
    {
        properties.set (field.name, schemaForOne (field));

        if (field.required)
            required.add (juce::var (field.name));
    }

    Obj schema;
    schema.set ("type", "object").set ("properties", properties);

    if (! required.isEmpty())
        schema.set ("required", juce::var (required));

    // Refused rather than ignored, matching what validateArgs does with an
    // argument nothing declares. A client told `velocty` is not a property can
    // fix it; one whose typo is silently dropped gets a default velocity and no
    // explanation.
    return schema.set ("additionalProperties", false);
}

juce::var schemaForOne (const ArgSpec& spec)
{
    Obj schema;

    if (const auto* type = jsonTypeOf (spec.kind); type[0] != '\0')
        schema.set ("type", type);

    schema.set ("description", spec.doc);

    if (spec.kind == ValueKind::object)
        return objectSchema (spec.fields);

    if (spec.kind == ValueKind::array)
    {
        if (spec.holdsBareElements())
            return schema.set ("items", schemaForOne (spec.fields.front()));

        if (! spec.fields.empty())
            return schema.set ("items", objectSchema (spec.fields));
    }

    return schema;
}

/** What a tool call gives back.

    A failure is a RESULT with isError, not a JSON-RPC error, and that is the
    specification's own distinction rather than a shortcut: a JSON-RPC error
    means the request could not be processed, while a tool that ran and refused
    has something the model should read and act on. Sending the second as the
    first hides the sentence that says what to do instead.

    The text content is the structured content, serialised. Every client can
    read text; not every client reads structuredContent yet, and a tool whose
    answer is invisible to half of them is a tool that does not work.
*/
juce::var toolResult (const juce::var& value, bool isError)
{
    Obj text;
    text.set ("type", "text").set ("text", juce::JSON::toString (value, false));

    juce::Array<juce::var> content;
    content.add (text);

    Obj result;
    result.set ("content", juce::var (content));

    if (isError)
        return result.set ("isError", true);

    return result.set ("structuredContent", value);
}

juce::var response (const juce::var& id, const juce::var& result)
{
    return Obj {}.set ("jsonrpc", "2.0").set ("id", id).set ("result", result);
}

juce::String uriFor (const GuideSection& section)
{
    return juce::String ("dew://guide/") + section.id;
}

juce::var callTool (ControlHost& host, const juce::var& params, Grant grant)
{
    const auto name = params[juce::Identifier ("name")].toString();
    const auto* op = findOp (name);

    if (op == nullptr)
        return toolResult (juce::var ("There is no tool called '" + name + "'."), true);

    // The whole of the permission check. One comparison against the operation's
    // own declared scope, so there is no second table of which tools are
    // dangerous that could disagree with what they do.
    if (grant != Grant::readWrite && op->scope == OpScope::write)
        return toolResult (juce::var ("'" + name
                                      + "' changes the project, and this client was approved for "
                                        "reading only. The person at the keyboard can change that "
                                        "in dew's MCP settings."),
                           true);

    const auto args = params[juce::Identifier ("arguments")];

    if (const auto fault = validateArgs (op->args, args); fault.isNotEmpty())
        return toolResult (juce::var (fault), true);

    const auto result = invoke (host, *op, args);

    return toolResult (result.ok ? result.value : juce::var (result.error), ! result.ok);
}

juce::var readResource (const juce::var& params)
{
    const auto uri = params[juce::Identifier ("uri")].toString();

    const auto* section = uri.startsWith ("dew://guide/")
                              ? findGuideSection (uri.fromLastOccurrenceOf ("/", false, false))
                              : nullptr;

    if (section == nullptr)
        return {};

    Obj contents;
    contents.set ("uri", uri)
        .set ("mimeType", "text/markdown")
        .set ("text", guideMarkdown (*section));

    juce::Array<juce::var> all;
    all.add (contents);

    return Obj {}.set ("contents", juce::var (all));
}

} // namespace

juce::var schemaForArgs (const std::vector<ArgSpec>& args)
{
    return objectSchema (args);
}

juce::var toolsList()
{
    juce::Array<juce::var> tools;

    for (const auto& op : ops())
    {
        Obj hints;

        // readOnlyHint is the same fact as OpSpec::scope, told to the client in
        // the vocabulary the protocol has for it. Derived, never restated.
        hints.set ("readOnlyHint", op.scope == OpScope::read)
            .set ("destructiveHint", op.scope == OpScope::write)
            .set ("openWorldHint", false);

        tools.add (Obj {}
                       .set ("name", op.name)
                       .set ("description", juce::String (op.summary) + "\n\n" + op.doc)
                       .set ("inputSchema", schemaForArgs (op.args))
                       .set ("annotations", hints));
    }

    return Obj {}.set ("tools", juce::var (tools));
}

juce::var resourcesList()
{
    juce::Array<juce::var> resources;

    for (const auto& section : guide())
        resources.add (Obj {}
                           .set ("uri", uriFor (section))
                           .set ("name", section.title)
                           .set ("description", section.summary)
                           .set ("mimeType", "text/markdown"));

    return Obj {}.set ("resources", juce::var (resources));
}

juce::var errorResponse (const juce::var& id, int code, const juce::String& message)
{
    return Obj {}
        .set ("jsonrpc", "2.0")
        .set ("id", id)
        .set ("error", Obj {}.set ("code", code).set ("message", message));
}

std::optional<juce::var> dispatch (ControlHost& host, const juce::var& message, Grant grant)
{
    const auto method = message[juce::Identifier ("method")].toString();
    const auto params = message[juce::Identifier ("params")];
    const auto id = message[juce::Identifier ("id")];

    // A notification has no id, and gets no response at all. The transport
    // turns "nothing" into 202 Accepted with an empty body.
    const auto isNotification = id.isVoid() || id.isUndefined();

    if (isNotification)
        return {};

    if (method == "initialize")
    {
        Obj capabilities;
        capabilities.set ("tools", Obj {}.set ("listChanged", false))
            .set ("resources", Obj {}.set ("subscribe", false).set ("listChanged", false));

        return response (id, Obj {}
                                 .set ("protocolVersion", protocolVersion)
                                 .set ("capabilities", capabilities)
                                 .set ("serverInfo", Obj {}
                                                         .set ("name", serverName)
                                                         .set ("title", "dew")
                                                         .set ("version", DEW_VERSION_STRING))
                                 .set ("instructions",
                                       "dew is a running DAW with a project open. Read "
                                       "dew://guide/index first, then call project_describe. Every "
                                       "write is one undo step."));
    }

    if (method == "ping")
        return response (id, Obj {});

    if (method == "tools/list")
        return response (id, toolsList());

    if (method == "tools/call")
        return response (id, callTool (host, params, grant));

    if (method == "resources/list")
        return response (id, resourcesList());

    if (method == "resources/read")
    {
        const auto contents = readResource (params);

        if (contents.isVoid())
            return errorResponse (id, invalidParams,
                                  "No resource at " + params[juce::Identifier ("uri")].toString()
                                      + ". Call resources/list.");

        return response (id, contents);
    }

    return errorResponse (id, methodNotFound, "dew does not implement '" + method + "'.");
}

} // namespace mcp
} // namespace dew::control
