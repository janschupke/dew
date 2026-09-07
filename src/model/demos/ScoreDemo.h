#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew::demo
{

/** Compiles an embedded .score into a fresh project and makes it inhabitable.

    Four of the ten demos get their notes this way, and every one of them wants
    the same three things afterwards, so they are here rather than copied out
    four times:

      - the channels the score never adopted are dropped. A bake starts from
        createDefault(), so a compiled project otherwise carries Kick, Snare,
        Bass and Lead with nothing to play and an empty Pattern 1 beside them;
      - every channel gets an insert of its own. addChannel falls back to insert
        1 when no track's id matches, so without this a five-channel score
        arrives with everything stacked on one fader;
      - the source travels in the document, so opening the demo puts real text
        in the Score tab and pressing Compile reproduces what is already
        playing. That is also what stops the .dew and the .score drifting: the
        demo is REGENERATED from the text.

    Returns an invalid tree if the score does not compile, which the caller must
    check - a demo that silently became a blank project is exactly the failure
    the demo gate cannot see, because a blank project has no clips to be wrong.
*/
juce::ValueTree compiledScore (const juce::String& fileName);

} // namespace dew::demo
