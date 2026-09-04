#pragma once

#include <vector>

#include <juce_core/juce_core.h>

namespace dew::control
{

/** One page of the usage guide.

    A declared table rather than a folder of Markdown files, for two reasons
    that both come down to having one source. The MCP resource wants text a
    model can read; the website wants structured data a React page can render,
    and it has no Markdown renderer - adding one would be a dependency for a
    page that is otherwise generated end to end. A table serves both, and the
    Markdown is derived from it rather than being the thing the site has to
    parse back.

    It also puts the prose in the library it describes, where a change to how an
    operation behaves and a change to what the guide says about it are one edit
    in one place.
*/
struct GuideSection
{
    /** The URI's last segment, and the website's anchor. Permanent: a guide's
        address appears in another guide's prose and in tool documentation, and
        a reference whose anchors move is a reference full of dead links. */
    const char* id;

    const char* title;

    /** One line, for a listing. */
    const char* summary;

    /** The body, as paragraphs. Plain prose - no Markdown syntax beyond blank
        lines between paragraphs, so that neither renderer has to parse
        anything. */
    std::vector<const char*> paragraphs;
};

/** Every page, in reading order.

    The first is the index, and it is the one a client should read first: it
    routes to the others rather than repeating them, the way ceum's own
    model/index does.
*/
const std::vector<GuideSection>& guide();

/** The section of this id, or nullptr. */
const GuideSection* findGuideSection (juce::StringRef id);

/** One section as the text an MCP resource hands back.

    The title as a heading and the paragraphs beneath it. Derived here rather
    than stored, so the table stays the only copy.
*/
juce::String guideMarkdown (const GuideSection&);

} // namespace dew::control
