#include "DocsMcp.h"
#include "JsonMain.h"

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

} // namespace

int main (int argc, char* argv[])
{
    return dew::tools::runJsonMain (argc, argv, "dew_mcp", usage, dew::docs::mcpJson);
}
