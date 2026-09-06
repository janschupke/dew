# ==============================================================================
# Compiles each JUCE module's own translation units exactly once.
#
# JUCE ships its modules as INTERFACE libraries that carry their .cpp/.mm files
# in INTERFACE_SOURCES, so every target that links one compiles it again. With
# nine layers, five tools and two test binaries that is not a rounding error:
# build/ci held 346 JUCE object files whose contents were only 41 distinct
# objects. 305 of them were byte-identical duplicates - 1.4 GB of a 3.1 GB tree,
# and 265 compiles of 25 files.
#
# The duplication was believed to be the price of the layering, on the grounds
# that each layer sees a different set of JUCE_MODULE_AVAILABLE_ defines. It is
# not: the defines differ and the generated code does not. Fifteen of the
# sixteen copies of juce_graphics.mm.o shared one checksum, no dew source reads
# a JUCE_MODULE_AVAILABLE_ macro, and every layer is already handed the one
# modules/ directory that contains all of JUCE anyway - juce_add_module puts the
# module PARENT on the include path. What makes the layering a link error is the
# set of link edges plus tests/SourceGateTests.cpp, and this file changes
# neither.
#
# So: one static library per module, built from that module's own sources, and
# an INTERFACE that hands consumers everything the JUCE target carried EXCEPT
# the sources.
# ==============================================================================

# The modules dew links, directly or transitively. juce_events and juce_core are
# here because the modules above them depend on them, not because a layer names
# them: juce_data_structures and juce_graphics both declare juce_events, which
# declares juce_core.
set(DEW_JUCE_MODULES
  juce_core
  juce_events
  juce_data_structures
  juce_graphics
  juce_gui_basics
  juce_gui_extra
  juce_audio_basics
  juce_audio_formats
  juce_dsp
  juce_audio_devices)

# dew_juce_build_once()
#
# Defines dew_<module> for every module above. Call it after
# dew_juce_config exists - the wrappers compile JUCE's sources with it, exactly
# as the layers did when they owned those sources.
function(dew_juce_build_once)
  # Every wrapper compiles with the SAME settings, which is the whole point: the
  # 25 translation units hash once instead of once per consuming target. The
  # union of the availability macros goes on all of them, because that is the
  # one thing that genuinely differed per layer before - and the identical
  # objects are the proof it was never load-bearing.
  set(all_available)
  foreach(mod IN LISTS DEW_JUCE_MODULES)
    list(APPEND all_available JUCE_MODULE_AVAILABLE_${mod}=1)
  endforeach()

  # The compile settings of all ten modules with the sources stripped out.
  # Reading INTERFACE_INCLUDE_DIRECTORIES and INTERFACE_COMPILE_DEFINITIONS off
  # the module target takes its usage requirements without taking
  # INTERFACE_SOURCES with them, which linking the module target would.
  add_library(dew_juce_settings INTERFACE)
  foreach(mod IN LISTS DEW_JUCE_MODULES)
    target_include_directories(dew_juce_settings SYSTEM INTERFACE
      $<TARGET_PROPERTY:${mod},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(dew_juce_settings INTERFACE
      $<TARGET_PROPERTY:${mod},INTERFACE_COMPILE_DEFINITIONS>)
  endforeach()

  # First pass: the libraries. Second pass wires them together, so a module may
  # name a sibling that has not been defined yet.
  foreach(mod IN LISTS DEW_JUCE_MODULES)
    get_target_property(module_sources ${mod} INTERFACE_JUCE_MODULE_SOURCES)

    if(NOT module_sources)
      message(FATAL_ERROR
        "dew: ${mod} has no INTERFACE_JUCE_MODULE_SOURCES. A JUCE upgrade has "
        "changed how juce_add_module records a module's own sources; "
        "cmake/JuceOnce.cmake has to be taught the new shape.")
    endif()

    add_library(dew_${mod} STATIC ${module_sources})

    target_link_libraries(dew_${mod}
      PRIVATE
        dew_juce_settings
        dew_juce_config
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    target_compile_definitions(dew_${mod} PRIVATE ${all_available})

    # What a consumer sees. Forwarding the module's own
    # INTERFACE_COMPILE_DEFINITIONS rather than spelling
    # JUCE_MODULE_AVAILABLE_${mod}=1 by hand matters: the property also carries
    # JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED, the NDEBUG/DEBUG genex, LINUX=1 and
    # gui_extra's JUCE_WEBVIEW_INTEROP_LIBRARY_VERSION. Forwarding it whole is
    # what keeps every one of dew's own translation units compiling with the
    # exact flags it had before.
    target_include_directories(dew_${mod} SYSTEM INTERFACE
      $<TARGET_PROPERTY:${mod},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(dew_${mod} INTERFACE
      $<TARGET_PROPERTY:${mod},INTERFACE_COMPILE_DEFINITIONS>)
    target_compile_features(dew_${mod} INTERFACE
      $<TARGET_PROPERTY:${mod},INTERFACE_COMPILE_FEATURES>)

    # Empty for every module dew uses on macOS and Linux - none of them ship a
    # libs/ directory - but the Windows branch of _juce_add_module_staticlib_paths
    # adds a path without checking that it exists, so on the Visual Studio
    # generator this is not empty and a wrapper that dropped it would not be a
    # faithful stand-in for the module.
    target_link_directories(dew_${mod} INTERFACE
      $<TARGET_PROPERTY:${mod},INTERFACE_LINK_DIRECTORIES>)
    target_link_options(dew_${mod} INTERFACE
      $<TARGET_PROPERTY:${mod},INTERFACE_LINK_OPTIONS>)
  endforeach()

  # Second pass: the module's own link dependencies, with sibling modules
  # remapped to their wrappers. A sibling forwarded verbatim would put JUCE's
  # sources back into every consumer and undo the whole exercise. Everything
  # else - "-framework Cocoa", juce::juce_atomic_wrapper, the pkgconfig targets
  # Linux builds get - carries no module sources and is forwarded as it is.
  foreach(mod IN LISTS DEW_JUCE_MODULES)
    get_target_property(module_deps ${mod} INTERFACE_LINK_LIBRARIES)

    if(NOT module_deps)
      continue()
    endif()

    set(mapped_deps)
    foreach(dep IN LISTS module_deps)
      string(REGEX REPLACE "^juce::" "" bare_dep "${dep}")

      if(bare_dep IN_LIST DEW_JUCE_MODULES)
        list(APPEND mapped_deps dew_${bare_dep})
      else()
        list(APPEND mapped_deps "${dep}")
      endif()
    endforeach()

    target_link_libraries(dew_${mod} INTERFACE ${mapped_deps})
  endforeach()
endfunction()
