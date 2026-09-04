#pragma once

#include <string>

#include "control/ControlGuide.h"
#include "control/ControlOps.h"
#include "control/McpProtocol.h"
#include "DocsJson.h"

namespace dew::docs
{

/** The operation table and the guide, as the JSON the website reads.

    The third emitter, and the same argument as the first two: the reference is
    GENERATED, so an operation cannot exist without appearing on the page and a
    page that stopped rendering one fails a test naming it. dew_docs does this
    for the score language by walking lang::schema(); this walks
    control::ops() and control::guide().

    Written with docs::JsonWriter rather than juce::JSON for the reason
    DocsJson.h states at length: this output is compared byte for byte against a
    committed copy, and juce::JSON's key order, indentation and number
    formatting are JUCE's to change.
*/

/** A ValueKind as the reference spells it.

    Delegates to control::nameOfKind rather than restating the table, so the
    name on the website is the name the validator uses. That function is the
    switch with no default, which is where a new kind is forced to declare
    itself.
*/
inline std::string kindName (control::ValueKind kind)
{
    return control::nameOfKind (kind);
}

inline void writeArgs (JsonWriter& json, const std::vector<control::ArgSpec>& args)
{
    json.beginArray();

    for (const auto& arg : args)
    {
        json.beginObject();
        json.key ("name");
        json.value (arg.name.toStdString());
        json.key ("kind");
        json.value (kindName (arg.kind));
        json.key ("required");
        json.value (arg.required);
        json.key ("doc");
        json.value (arg.doc);

        // The convention an array of bare values uses: one field, unnamed. Told
        // to the page as a flag rather than left for it to re-derive, because
        // re-deriving it is where a second reading of the rule would come from.
        json.key ("holdsBareElements");
        json.value (arg.holdsBareElements());

        json.key ("fields");
        writeArgs (json, arg.fields);
        json.endObject();
    }

    json.endArray();
}

inline std::string mcpJson()
{
    JsonWriter json;

    json.beginObject();

    json.key ("protocolVersion");
    json.value (control::mcp::protocolVersion);

    json.key ("guide");
    json.beginArray();

    for (const auto& section : control::guide())
    {
        json.beginObject();
        json.key ("id");
        json.value (section.id);
        json.key ("title");
        json.value (section.title);
        json.key ("summary");
        json.value (section.summary);

        json.key ("paragraphs");
        json.beginArray();

        for (const auto* paragraph : section.paragraphs)
            json.value (paragraph);

        json.endArray();
        json.endObject();
    }

    json.endArray();

    json.key ("tools");
    json.beginArray();

    for (const auto& op : control::ops())
    {
        json.beginObject();
        json.key ("name");
        json.value (op.name);

        // "read" or "write", which is the whole of the permission model and the
        // thing a reader most wants to see beside a tool's name.
        json.key ("scope");
        json.value (op.scope == control::OpScope::read ? "read" : "write");

        json.key ("summary");
        json.value (op.summary);
        json.key ("doc");
        json.value (op.doc);
        json.key ("args");
        writeArgs (json, op.args);
        json.endObject();
    }

    json.endArray();
    json.endObject();

    return json.str();
}

} // namespace dew::docs
