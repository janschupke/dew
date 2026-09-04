#include <fstream>
#include <iostream>
#include <string>

#include "DocsMcp.h"

namespace
{

constexpr const char* usage = R"(dew_mcp - write the MCP reference the website reads

Every operation dew exposes over MCP, and the usage guide that ships as its
resources, as JSON from control::ops() and control::guide(). The output is
COMMITTED under website/src/generated, which is what lets the site build with no
C++ toolchain.

Usage:
  dew_mcp schema <out.json>

A test in dew_tests holds the committed copy against what this writes, and CI
writes it again in a second process and cmps. Regenerate it in the commit that
changes the operation table; never edit it by hand.
)";

int fail (const std::string& message)
{
    std::cerr << "dew_mcp: " << message << std::endl;
    return 1;
}

/** Written with an explicit "\n" and in binary mode, for the reason docs_main
    gives: juce::File::replaceWithText defaults its line endings to CRLF, and
    the in-process test would then compare that file happily against itself
    while cmp in CI compared it against an LF copy. */
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

    if (! write (path, dew::docs::mcpJson()))
        return fail ("could not write " + path);

    std::cout << "wrote " << path << std::endl;

    return 0;
}
