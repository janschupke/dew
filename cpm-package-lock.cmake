# ==============================================================================
# dew package lock - the single source of truth for every dependency.
#
# Adding a dependency means adding ONE declaration here, plus its expected
# commit SHA in cmake/DependencyPins.cmake. Nothing else in the build fetches
# anything.
#
# Regenerate after changing a declaration:
#     cmake --build build/release --target cpm-update-package-lock
#
# Why GIT_TAG and not a raw commit SHA: CMake's GIT_SHALLOW clones with
# `--depth 1 --no-single-branch`, which only fetches branch tips, so an
# arbitrary commit hash is not guaranteed to be present. We therefore fetch
# shallow by tag (cheap: JUCE is 19 MB / ~3 s this way) and then VERIFY the
# resolved commit against DEW_PIN_<name>. A moved upstream tag fails configure
# instead of silently changing the code we build.
# ==============================================================================

# JUCE 9.0.1 - https://juce.com - commercial or AGPLv3
CPMDeclarePackage(JUCE
  NAME           JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        9.0.1
  GIT_SHALLOW    ON
)

# Catch2 v3.16.0 - test framework - BSL-1.0
CPMDeclarePackage(Catch2
  NAME           Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.16.0
  GIT_SHALLOW    ON
)
