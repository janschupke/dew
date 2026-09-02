# ==============================================================================
# Immutable identity for every dependency in cpm-package-lock.cmake.
#
# The lock fetches by TAG (so the clone can be shallow); this file records the
# commit each tag must resolve to, and dew_verify_pin() enforces it after the
# fetch. If upstream ever re-points a tag, configure fails loudly rather than
# building different code under the same version number.
#
# To bump a dependency: change GIT_TAG in the lock, then run
#     git ls-remote https://github.com/<org>/<repo>.git refs/tags/<tag>^{}
# and paste the resulting commit here. (The ^{} suffix dereferences annotated
# tags to the commit they point at - without it you get the tag object's own
# hash, which is NOT what `git rev-parse HEAD` reports in a checkout.)
# ==============================================================================

set(DEW_PIN_JUCE   "e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8") # tag 9.0.1
set(DEW_PIN_Catch2 "317ac1ed4c0bb6e6b91eafc817e05c488feffcb3") # tag v3.16.0

# ------------------------------------------------------------------------------
# dew_verify_pin(<name> <source_dir>)
#
# Fails closed: a source tree we cannot identify is an error, never a skip.
# ------------------------------------------------------------------------------
function(dew_verify_pin name source_dir)
  set(expected "${DEW_PIN_${name}}")

  if(NOT expected)
    message(FATAL_ERROR
      "dew: no DEW_PIN_${name} recorded in cmake/DependencyPins.cmake.\n"
      "Every package in cpm-package-lock.cmake must have an expected commit SHA.")
  endif()

  if(NOT EXISTS "${source_dir}/.git")
    message(FATAL_ERROR
      "dew: cannot verify ${name} - no git metadata at ${source_dir}.\n"
      "The dependency's identity cannot be established, so the build refuses to\n"
      "continue. Clear the CPM source cache and reconfigure:\n"
      "    rm -rf \"$ENV{CPM_SOURCE_CACHE}\"")
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse HEAD
    OUTPUT_VARIABLE actual
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE git_error
    RESULT_VARIABLE git_result)

  if(NOT git_result EQUAL 0)
    message(FATAL_ERROR "dew: `git rev-parse HEAD` failed for ${name} in ${source_dir}: ${git_error}")
  endif()

  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
      "dew: dependency pin mismatch for ${name}.\n"
      "  expected: ${expected}  (cmake/DependencyPins.cmake)\n"
      "  actual:   ${actual}\n"
      "The upstream tag may have been re-pointed, or the cached source is stale.\n"
      "If this change is intended, update DEW_PIN_${name}; otherwise clear the\n"
      "CPM source cache and reconfigure.")
  endif()

  message(STATUS "dew: ${name} pin verified @ ${actual}")
endfunction()
