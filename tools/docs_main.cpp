#include "DocsSchema.h"
#include "JsonMain.h"

namespace
{

constexpr const char* usage = R"(dew_docs - write the reference the website reads

The score language, as JSON, from lang::schema(). The output is COMMITTED under
website/src/generated, which is what lets the site build with no C++ toolchain -
so a website change waits behind npm rather than behind a JUCE build, and a host
that has never heard of CMake can serve it.

Usage:
  dew_docs schema <out.json>

A test in dew_tests holds the committed copy against what this writes, and CI
writes it again in a second process and cmps. Regenerate it in the commit that
changes the schema; never edit it by hand.
)";

} // namespace

int main (int argc, char* argv[])
{
    return dew::tools::runJsonMain (argc, argv, "dew_docs", usage,
                                    [] { return dew::docs::schemaJson(); });
}
