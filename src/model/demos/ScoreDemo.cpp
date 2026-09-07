#include "model/demos/ScoreDemo.h"

#include "lang/Compile.h"

#include "model/DemoBuilders.h"
#include "model/DemoLibrary.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ScoreBake.h"

namespace dew::demo
{

juce::ValueTree compiledScore (const juce::String& fileName)
{
    const auto source = DemoLibrary::jsonFor (fileName);

    if (source.isEmpty())
        return {};

    const auto result = lang::compile (source.toStdString());

    if (! result.ok())
        return {};

    BakeReport report;
    auto project = ScoreBake::toNewProject (*result.score, report);

    pruneUnplayedChannels (project);
    pruneEmptyLanes (project);
    rebuildInserts (project);

    ProjectEdits::setScoreSource (project, source, fileName, nullptr);

    return project;
}

} // namespace dew::demo
