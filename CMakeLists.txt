# ==============================================================================
# dew - a desktop digital synth DAW
#
# Build with the presets, not raw flags:
#     cmake --preset release && cmake --build --preset release
# ==============================================================================

# 3.25 not 3.22: JUCE 9 needs 3.22, CMakePresets v6 needs 3.25.
cmake_minimum_required(VERSION 3.25)

# LANGUAGES C CXX is load-bearing - JUCE 9 hard-errors without a C compiler
# enabled, because zlib/libpng/libflac are now built in C language mode.
project(dew VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# ------------------------------------------------------------------------------
# Compile cache. Absence is a slowdown, never a build failure.
# ------------------------------------------------------------------------------
find_program(DEW_CCACHE_PROGRAM ccache)
if(DEW_CCACHE_PROGRAM)
  execute_process(COMMAND "${DEW_CCACHE_PROGRAM}" --version
                  OUTPUT_VARIABLE _ccache_version_out OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" DEW_CCACHE_VERSION "${_ccache_version_out}")
  set(CMAKE_C_COMPILER_LAUNCHER   "${DEW_CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${DEW_CCACHE_PROGRAM}")
  message(STATUS "dew: using ccache ${DEW_CCACHE_VERSION} (${DEW_CCACHE_PROGRAM})")
else()
  message(STATUS "dew: ccache not found - cold rebuilds will be slow")
endif()

# ------------------------------------------------------------------------------
# Dependencies. CPM is vendored (cmake/CPM.cmake, sha256-verified) so bootstrap
# needs no network; every version lives in cpm-package-lock.cmake; every commit
# is verified against cmake/DependencyPins.cmake after the fetch.
# ------------------------------------------------------------------------------
find_package(Git REQUIRED)

include(CPM)
include(DependencyPins)
include(ThirdPartyManifest)

CPMUsePackageLock(cpm-package-lock.cmake)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS   OFF CACHE BOOL "" FORCE)

CPMGetPackage(JUCE)
dew_verify_pin(JUCE "${JUCE_SOURCE_DIR}")
dew_record_third_party(JUCE "9.0.1" "${DEW_PIN_JUCE}"
  "https://github.com/juce-framework/JUCE" "AGPLv3 or commercial" "${JUCE_SOURCE_DIR}")

if(DEW_BUILD_TESTS)
  CPMGetPackage(Catch2)
  dew_verify_pin(Catch2 "${Catch2_SOURCE_DIR}")
  dew_record_third_party(Catch2 "v3.16.0" "${DEW_PIN_Catch2}"
    "https://github.com/catchorg/Catch2" "BSL-1.0" "${Catch2_SOURCE_DIR}")
  list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
  include(CTest)
  include(Catch)
endif()

dew_write_third_party_manifest("${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY.md")

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(tools)

if(DEW_BUILD_TESTS)
  add_subdirectory(tests)
endif()
