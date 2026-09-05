#pragma once

#include <juce_core/juce_core.h>

namespace dew::diagnostics
{

/** What dew leaves behind about its own running, so a fault has evidence.

    dew used to write nothing at all. No logger was installed, no crash handler
    was set, and there was no try/catch anywhere in the tree - so a segfault
    took the window off the screen and left the person in front of it with the
    word "crashed" and nothing else. The one that prompted this took a whole
    afternoon to find, and only because macOS had written the report dew had
    not.

    ### Deliberately thin

    macOS already writes a fully symbolicated report to
    ~/Library/Logs/DiagnosticReports, and it is better than anything this could
    produce: every thread, every frame, the register state. Duplicating it would
    be work spent competing with the operating system.

    So this writes the two things that report does NOT give: a running log of
    what dew was doing before the fault, and a marker that says a fault happened
    at all - which is what lets the NEXT launch tell somebody, rather than
    leaving them to know to go looking.

    Nothing is sent anywhere. JUCE_USE_CURL is 0 and JUCE_WEB_BROWSER is 0; the
    files sit in the log folder until the person reads or deletes them.
*/

/** Installs the log file and the crash handler. Called once, first thing, and
    before anything that could fault - which is why it takes the version line
    rather than reading it from a Settings that does not exist yet.
*/
void begin (const juce::String& welcome);

/** Removes the logger, so nothing writes through a dangling one during
    shutdown. Safe to call without a matching begin. */
void end();

/** Where the log and any crash markers live. Empty if begin has not run. */
juce::File logDirectory();

/** One line, timestamped, into the log. Does nothing when begin has not run,
    which is what makes it callable from a test and from dew_shot. */
void log (const juce::String& message);

/** The crash marker a PREVIOUS launch left, or an invalid File.

    Never this launch's: the marker is created by the handler at the moment of
    the fault, so a run that has not crashed has not written one.
*/
juce::File unreportedCrash();

/** Marks a marker as told, so it is said once and not at every launch after.
    Renamed rather than deleted - the backtrace in it is the evidence, and
    deleting the evidence to remember having seen it would be a poor trade. */
void markReported (const juce::File&);

} // namespace dew::diagnostics
