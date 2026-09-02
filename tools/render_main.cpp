#include <juce_core/juce_core.h>

#include "BuildInfo.h"

// Phase 1: prints provenance and exits. Phase 3 replaces this with
//   dew_render <project.dew> <out.wav> [--seconds N] [--pattern ID | --song]
int main (int argc, char* argv[])
{
    juce::ignoreUnused (argc, argv);

    std::cout << dew::BuildInfo::summary() << std::endl;
    std::cout << "dew_render: offline renderer (not yet implemented - phase 3)" << std::endl;

    return 0;
}
