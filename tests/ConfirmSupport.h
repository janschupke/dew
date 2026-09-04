#pragma once

#include <functional>
#include <utility>

#include "ui/ConfirmPanel.h"

/** Answering a confirmation without one opening.

    JUCE_MODAL_LOOPS_PERMITTED is 0, so a DialogWindow cannot be driven
    headlessly - which is the whole reason every destructive action in dew goes
    through a ConfirmHook rather than calling ConfirmPanel::show itself. A test
    replaces the hook with one of these.

    Shared because four components carry the hook, and a test that answers "yes"
    by writing the lambda out again is a test that can quietly answer "no".
*/
namespace dew::testing
{

/** Says yes at once. For the tests that are about what a deletion DOES, not
    about the asking - the ones that existed before there was a question. */
inline ConfirmHook alwaysConfirm()
{
    return [] (ConfirmPanel::Request, std::function<void()> confirmed)
    {
        if (confirmed)
            confirmed();
    };
}

/** Says nothing, and remembers what it was asked.

    Holding the callback rather than dropping it is what lets one test assert
    both halves: that nothing happened while the question was open, and that the
    right thing happens when it is answered.
*/
struct ConfirmRecorder
{
    ConfirmHook hook()
    {
        return [this] (ConfirmPanel::Request request, std::function<void()> confirmed)
        {
            ++timesAsked;
            lastRequest = std::move (request);
            answerYes = std::move (confirmed);
        };
    }

    /** Answers the question that is waiting. */
    void confirm()
    {
        if (answerYes)
            answerYes();
    }

    int timesAsked = 0;
    ConfirmPanel::Request lastRequest;
    std::function<void()> answerYes;
};

} // namespace dew::testing
