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

# The prerelease identifier, and the reason it is not in project(VERSION): CMake
# accepts non-negative integers there and nothing else, so `0.2.0-rc.1` will not
# parse. SemVer needs somewhere to put the suffix, so it gets its own line.
#
# Empty for a release; `-rc.1`, `-beta.2` and so on otherwise. It reaches the
# running binary through DEW_VERSION_STRING (src/CMakeLists.txt) and NOT through
# the VERSION argument of juce_add_gui_app - CFBundleShortVersionString is
# specified as a numeric triple, and Apple rejects a bundle whose version is not.
#
# Not a cache variable, deliberately. A -D override would make the binary
# disagree with the file, and the FILE is what the release workflow reads to
# check the git tag against - so these two lines are the only place a version is
# written by hand, and BuildInfoTests holds the running binary against them.
set(DEW_VERSION_SUFFIX "")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Declared, not merely referenced. Before this, DEW_BUILD_TESTS was set only by
# CMakePresets.json, so a plain `cmake -S . -B build` configured with no tests
# and no Catch2 and said nothing about it.
option(DEW_BUILD_TESTS "Build the test suite" ON)

# THIRD_PARTY.md is written into the SOURCE TREE at configure time, and its
# toolchain table records the CMake, compiler and ccache versions that wrote it.
# That is exactly right for the macOS gate, which diffs the committed copy - and
# exactly wrong anywhere else: a configure on a Windows or Linux runner, or a
# `dist` build for packaging, would rewrite the file with a different toolchain
# and leave the tree dirty for a reason that has nothing to do with the
# dependencies. Those configures turn this OFF; the recording still happens, so
# the licence files a package ships are still located.
option(DEW_WRITE_THIRD_PARTY "Write THIRD_PARTY.md into the source tree" ON)

# Declared here rather than passed as a raw -DCMAKE_OSX_ARCHITECTURES, so the
# `dist` preset stays platform-neutral and CMake keeps the platform knowledge -
# the same arrangement CMAKE_OSX_DEPLOYMENT_TARGET already has below. A preset
# naming an Apple variable would make every Linux and Windows configure warn
# about a manually-specified variable the project never used.
#
# Off by default because it doubles the compile, and a developer building to run
# the thing wants one architecture. The release build wants both: GitHub's macOS
# runners are Apple Silicon, so without this every Intel Mac gets nothing.
option(DEW_UNIVERSAL_BINARY "On Apple, build arm64 and x86_64 in one binary" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

if(APPLE AND DEW_UNIVERSAL_BINARY AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Apple architectures" FORCE)
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

if(DEW_WRITE_THIRD_PARTY)
  dew_write_third_party_manifest("${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY.md")
endif()

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(tools)

if(DEW_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ------------------------------------------------------------------------------
# Packaging. Last, because it needs the `dew` target to exist.
# ------------------------------------------------------------------------------
include(Packaging)
