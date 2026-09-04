#include <catch2/catch_test_macros.hpp>

#include "DocsJson.h"

using namespace dew::docs;

TEST_CASE ("a number is written by integer arithmetic", "[docs][json]")
{
    // Three decimals, trailing zeros dropped. Never snprintf and never
    // std::to_string(double): both go through the C locale, so a machine whose
    // LC_NUMERIC uses a comma writes 0,06 - which is not JSON - and this file
    // is compared byte for byte against a copy committed elsewhere.
    const auto emit = [] (float v)
    {
        JsonWriter json;
        json.value (v);
        return json.str();
    };

    CHECK (emit (0.06f) == "0.06\n");
    CHECK (emit (0.1f) == "0.1\n");
    CHECK (emit (1.5f) == "1.5\n");
    CHECK (emit (2.0f) == "2\n");
    CHECK (emit (11.0f) == "11\n");
    CHECK (emit (0.07f) == "0.07\n");
    CHECK (emit (0.0f) == "0\n");
    CHECK (emit (-0.5f) == "-0.5\n");
}

TEST_CASE ("a string literal is a string, not a bool", "[docs][json]")
{
    // The whole reason JsonWriter has a const char* overload. `const char*`
    // converts to `bool` by a STANDARD conversion and to std::string_view only
    // by a user-defined one, so without that overload every nameOf() in the
    // schema binds to the bool one - and the first generated file really did
    // come out as `"name": true` for all thirty-one value kinds.
    JsonWriter json;

    json.beginObject();
    json.key ("name");
    json.value ("song");
    json.key ("topLevel");
    json.value (true);
    json.endObject();

    CHECK (json.str() == "{\n  \"name\": \"song\",\n  \"topLevel\": true\n}\n");
}

TEST_CASE ("members come out in the order they were written", "[docs][json]")
{
    // No map anywhere in the writer, which is the first half of the
    // determinism claim. The second half is that nothing sorts.
    JsonWriter json;

    json.beginObject();
    json.key ("zebra");
    json.value (1);
    json.key ("alpha");
    json.value (2);
    json.endObject();

    CHECK (json.str() == "{\n  \"zebra\": 1,\n  \"alpha\": 2\n}\n");
}

TEST_CASE ("an empty container writes no newline inside itself", "[docs][json]")
{
    JsonWriter json;

    json.beginObject();
    json.key ("members");
    json.beginArray();
    json.endArray();
    json.endObject();

    CHECK (json.str() == "{\n  \"members\": []\n}\n");
}

TEST_CASE ("a backslash is escaped before anything that adds one", "[docs][json]")
{
    // Backslash first, or every escape this adds is escaped again by the next
    // replacement - the order cmake/GenStrings.cmake already argues for.
    JsonWriter json;
    json.value ("a\\b\"c\nd\te");

    CHECK (json.str() == "\"a\\\\b\\\"c\\nd\\te\"\n");
}

TEST_CASE ("a control character becomes a unicode escape", "[docs][json]")
{
    JsonWriter json;
    json.value (std::string_view ("a\x01"
                                  "b",
                                  3));

    CHECK (json.str() == "\"a\\u0001b\"\n");
}

TEST_CASE ("utf-8 above the ascii range passes through", "[docs][json]")
{
    // JSON permits it, and it keeps the schema's own prose readable in a diff.
    JsonWriter json;
    json.value ("café");

    CHECK (json.str() == "\"café\"\n");
}
