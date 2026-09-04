#include <fstream>
#include <iostream>
#include <string>

#include "DocsSchema.h"

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

int fail (const std::string& message)
{
    std::cerr << "dew_docs: " << message << std::endl;
    return 1;
}

/** Written with an explicit "\n" and in binary mode.

    Not juce::File::replaceWithText, which this tool could not call anyway: its
    lineEndings parameter DEFAULTS to "\r\n", so the obvious call writes a CRLF
    file. The in-process test would then compare that file happily against
    itself while cmp in CI compared it against an LF copy and failed - or worse,
    the first commit would land CRLF and everything would agree on the wrong
    thing.
*/
bool write (const std::string& path, const std::string& text)
{
    std::ofstream out (path, std::ios::binary | std::ios::trunc);

    if (! out)
        return false;

    out << text;

    return out.good();
}

} // namespace

int main (int argc, char* argv[])
{
    const std::string command = argc > 1 ? argv[1] : "";

    if (command == "--help" || command == "-h" || command.empty())
    {
        std::cout << usage << std::endl;
        return command.empty() ? 1 : 0;
    }

    if (command != "schema")
        return fail ("unknown command '" + command + "' - see --help");

    if (argc < 3)
        return fail ("schema needs an output path - see --help");

    const std::string path = argv[2];

    if (! write (path, dew::docs::schemaJson()))
        return fail ("could not write " + path);

    std::cout << "wrote " << path << std::endl;

    return 0;
}
