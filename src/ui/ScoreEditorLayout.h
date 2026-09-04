#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "lang/Diagnostics.h"
#include "ui/design/Tokens.h"

namespace dew
{

/** What the score tab's two translation units both need to agree on.

    INTERNAL: only ScoreEditorComponent.cpp and ScoreEditorPaint.cpp include it.

    The font ladder is here rather than in Tokens.h as an array, because a table
    inside the token file would satisfy the unused-token gate for all four rungs
    without the application ever quoting one - that gate reads every file EXCEPT
    Tokens.h, for exactly that reason.

    colourFor is here because the overlay draws a squiggle in it and the editor
    draws the diagnostic list in it, and a list that disagreed with the squiggle
    above it about which of two problems is the error would be a strange thing
    to look at.
*/

/** The sizes the score's text steps through, smallest first.

    Here rather than in Tokens.h as an array, because a table inside the token
    file would satisfy the unused-token gate for all four rungs without the
    application ever quoting one - the gate reads every file EXCEPT Tokens.h
    for exactly that reason.
*/
constexpr float fontSteps[] = { tokens::type::codeSmall, tokens::type::codeBody,
                                tokens::type::codeLarge, tokens::type::codeHuge };

constexpr int numSteps = (int) (sizeof (fontSteps) / sizeof (fontSteps[0]));

/** codeBody: the rung the tab opens at, named once rather than spelled as 1. */
constexpr int bodyStep = 1;

static_assert (fontSteps[0] < fontSteps[1] && fontSteps[1] < fontSteps[2]
                   && fontSteps[2] < fontSteps[3],
               "the steps have to increase, or bigger and smaller swap over");

// exactlyEqual, not ==: the ci preset builds -Wfloat-equal, and these two are
// the same constant reached two ways rather than two computed numbers.
static_assert (juce::exactlyEqual (fontSteps[bodyStep], tokens::type::codeBody),
               "the default step has to be the rung it is named after");

/** What the Score tab shows when a project has no score in it.

    A blank rectangle is indistinguishable from a feature that is not there, and
    a language nobody can see the shape of is a language nobody writes. This one
    compiles, and a test renders it to check it is audible and does not clip -
    the first thing anybody presses Compile on should make a sound.

    It is NOT written into the project until it is edited or compiled, so
    opening the tab does not dirty a project nobody has touched.
*/
const char* const starterScoreText =
    R"SCORE(// A score describes a whole song as text: its key, its chords, its sections,
// and a rule per instrument for what to play over them. Press Compile, or
// Command-R, and it becomes patterns and clips you can edit like any others.
//
// This one plays. Change a chord, change `variance`, compile again.

song {
  title "Untitled"
  tempo 110 bpm
  meter 4/4
  key   A minor
  seed  0x5C0DED
}

channel pad {
  mixer    1
  range    C3..C5
  velocity 54 +- 5
}

channel bass {
  mixer    2
  range    E1..E3
  velocity 74 +- 4
}

channel lead {
  mixer    3
  range    A3..A5
  velocity 66 +- 8
}

voicing warm {
  size     4 voices
  spread   drop2
  register C3..C5
  motion   smooth
}

rhythm held  { 1/1 }
rhythm pulse { 1/4 1/4 1/2 }
rhythm line  { 1/8 1/8 1/4 }

harmony loop {
  i | bVI | bIII | bVII
}

section verse {
  length 4 bars
  harmony loop

  part pad {
    chords with warm
    rhythm held
  }

  part bass {
    line root
    rhythm pulse
  }

  part lead {
    melody {
      rhythm   line
      contour  arch
      strong   chord-tones
      variance 0.3
      mute     1 of 4
    }
  }
}

arrangement {
  verse
  verse
}
)SCORE";

/** How many rows of diagnostics are shown before the list scrolls. */
constexpr int diagnosticRows = 4;

inline juce::Colour colourFor (lang::Severity severity)
{
    return severity == lang::Severity::error ? tokens::colour::danger : tokens::colour::warning;
}

} // namespace dew
