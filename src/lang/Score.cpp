#include "lang/Score.h"

#include <algorithm>

namespace dew::lang
{

std::vector<Note> Score::flatten() const
{
    std::vector<Note> out;

    for (const auto& clip : clips)
    {
        if (clip.pattern < 0 || clip.pattern >= (int) patterns.size())
            continue;

        const auto& pattern = patterns[(std::size_t) clip.pattern];
        const auto offset = clip.startBar * stepsPerBar();

        for (const auto& note : pattern.notes)
        {
            auto moved = note;
            moved.startStep = offset + note.startStep;
            out.push_back (moved);
        }
    }

    std::stable_sort (out.begin(), out.end(),
                      [] (const Note& a, const Note& b)
                      {
                          if (a.startStep != b.startStep)
                              return a.startStep < b.startStep;
                          if (a.track != b.track)
                              return a.track < b.track;
                          return a.pitch < b.pitch;
                      });

    return out;
}

int Score::noteCount() const noexcept
{
    auto count = 0;

    for (const auto& clip : clips)
        if (clip.pattern >= 0 && clip.pattern < (int) patterns.size())
            count += (int) patterns[(std::size_t) clip.pattern].notes.size();

    return count;
}
} // namespace dew::lang
