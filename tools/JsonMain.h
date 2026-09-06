#pragma once

#include <fstream>
#include <functional>
#include <iostream>
#include <string>

namespace dew::tools
{

/** The whole of a tool whose job is "write one JSON file".

    dew_docs and dew_mcp were the same program. Their mains were identical
    except for which function produced the string, and so were the fail() and
    write() helpers above them - the largest duplicated region in the tree, and
    across the one boundary where duplication buys least, since neither tool has
    a reason of its own to differ from the other.

    Written with an explicit "\n" and in binary mode. NOT
    juce::File::replaceWithText, which these two could not call anyway: its
    lineEndings parameter DEFAULTS to "\r\n", so the obvious call writes a CRLF
    file. The in-process test would then compare that file happily against
    itself while cmp in CI compared it against an LF copy and failed - or worse,
    the first commit would land CRLF and everything would agree on the wrong
    thing.

    argv by hand rather than through tools/CliArgs.h, which is where the other
    three tools go. Not an oversight: dew_docs and dew_mcp link dew_lang and
    dew_control and NOT juce_core, so CliArgs - a juce::StringPairArray - is not
    reachable from either without giving both tools a JUCE include path for two
    positional arguments. The parsing they need is `<command> <path>`.
*/
inline int runJsonMain (int argc, char* argv[], const char* toolName, const char* usage,
                        const std::function<std::string()>& json)
{
    const auto fail = [toolName] (const std::string& message)
    {
        std::cerr << toolName << ": " << message << std::endl;
        return 1;
    };

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

    std::ofstream out (path, std::ios::binary | std::ios::trunc);
    out << json();

    if (! out.good())
        return fail ("could not write " + path);

    std::cout << "wrote " << path << std::endl;

    return 0;
}

} // namespace dew::tools
