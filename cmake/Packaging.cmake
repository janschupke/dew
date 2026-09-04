# ==============================================================================
# install() rules and CPack configuration - how a build becomes something a
# person can download.
#
# Included from the top-level CMakeLists.txt, and doing nothing unless a `cpack`
# is actually run. It is separate from src/CMakeLists.txt on purpose: that file
# says what the program IS, and this one says what a stranger receives. They
# change for different reasons.
#
# The generators, and why each one:
#
#   macOS     DragNDrop   a .dmg with the bundle and a link to /Applications.
#                         hdiutil only - the popular create-dmg drives Finder
#                         over Apple events and cannot run on a headless
#                         runner.
#   Windows   INNOSETUP   NOT NSIS. NSIS was removed from the windows-2025
#                         runner image, which is what windows-latest now means,
#                         and CPack's NSIS generator exposes no way to ask for a
#                         per-user install - the execution level is baked into a
#                         template. Inno Setup is on both images and takes any
#                         [Setup] directive through CPACK_INNOSETUP_SETUP_*.
#             ZIP         the portable copy, secondary: no uninstall entry, no
#                         Start menu, no file association.
#   Linux     TGZ         the tarball. The AppImage is built from this same
#                         install tree by linuxdeploy in the release workflow -
#                         AppImage is not a CPack generator and pretending it is
#                         would mean an External generator wrapping a shell
#                         script that the workflow can run directly.
#
# CPACK_PACKAGE_FILE_NAME carries NO version, deliberately. The website links
# releases/latest/download/<name>, which GitHub resolves server-side to the
# newest release carrying an asset of that name - so the download button needs
# no API call, no generated file, and cannot advertise a build that has not
# finished. The version travels in the DMG volume name, in the installer's
# window, and in the binary. See .ai/rules/release.md.
#
# Two generators on Windows want two different names from one build, and CPack
# has no per-generator name variable, so the workflow passes
# -D CPACK_PACKAGE_FILE_NAME per invocation. The default below is what a local
# `cpack` gets.
# ==============================================================================

set(DEW_PACKAGE_VERSION "${PROJECT_VERSION}${DEW_VERSION_SUFFIX}")

# ------------------------------------------------------------------------------
# The licence texts, inside the package.
#
# AGPLv3 obliges an offer of the corresponding source to whoever receives the
# binary, and the person holding a .dmg has never seen this repository's README.
# So both licences travel with the program: dew's own, and the one JUCE ships -
# located by dew_record_third_party rather than guessed at here, so a dependency
# that renames its licence file cannot silently drop out.
# ------------------------------------------------------------------------------
# Named pairs rather than a list of paths: two files called LICENSE cannot share
# a directory, so each one is copied to a name that says whose it is. Spelled
# here rather than derived from the path - JUCE's licence lives in a CPM cache
# directory named after a hash, and "9baa-LICENSE.txt" tells a reader nothing.
set(DEW_PACKAGED_DOCS
  "${CMAKE_SOURCE_DIR}/LICENSE"       "dew-LICENSE.txt"
  "${CMAKE_SOURCE_DIR}/THIRD_PARTY.md" "THIRD_PARTY.md")

if(EXISTS "${JUCE_SOURCE_DIR}/LICENSE.md")
  list(APPEND DEW_PACKAGED_DOCS "${JUCE_SOURCE_DIR}/LICENSE.md" "JUCE-LICENSE.md")
endif()

set(DEW_STAGED_DOCS "")
list(LENGTH DEW_PACKAGED_DOCS dew_doc_count)
math(EXPR dew_doc_last "${dew_doc_count} - 1")

foreach(i RANGE 0 ${dew_doc_last} 2)
  math(EXPR j "${i} + 1")
  list(GET DEW_PACKAGED_DOCS ${i} dew_doc_source)
  list(GET DEW_PACKAGED_DOCS ${j} dew_doc_name)

  set(dew_doc_staged "${CMAKE_CURRENT_BINARY_DIR}/packaged-docs/${dew_doc_name}")
  configure_file("${dew_doc_source}" "${dew_doc_staged}" COPYONLY)
  list(APPEND DEW_STAGED_DOCS "${dew_doc_staged}")
endforeach()

if(APPLE)
  # Inside the bundle, as resources, rather than beside it in the .dmg: the .app
  # is dragged somewhere on its own and anything left in the image is left
  # behind. A licence that does not travel with the binary is not an offer.
  target_sources(dew PRIVATE ${DEW_STAGED_DOCS})

  # TARGET_DIRECTORY, and it is load-bearing. Source file properties are scoped
  # to the DIRECTORY that sets them, and this file is included from the top
  # level while `dew` is defined in src/ - so the plain form set the property
  # somewhere nothing would ever read it, and the first .dmg came out with a
  # Resources folder holding JUCE's nib and neither licence.
  set_source_files_properties(${DEW_STAGED_DOCS}
    TARGET_DIRECTORY dew
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

  # No /Applications symlink here: the DragNDrop generator creates one itself
  # for a package holding a bundle, and a second attempt fails the pack with
  # "File exists". The drag target is all that affordance has ever been - the
  # alternative, a committed .DS_Store laying out icon positions, is usually
  # made by driving Finder over Apple events, which cannot run headless at all.
  install(TARGETS dew BUNDLE DESTINATION "." COMPONENT dew)
else()
  # Flat on Windows, so the zip extracts to one folder holding dew.exe and the
  # installer has nothing to nest. Under bin/ on Linux, which is where
  # linuxdeploy looks and where a tarball unpacked into /opt belongs.
  if(WIN32)
    set(DEW_RUNTIME_DIR ".")
    set(DEW_DOC_DIR ".")
  else()
    set(DEW_RUNTIME_DIR "bin")
    set(DEW_DOC_DIR "share/doc/dew")
  endif()

  install(TARGETS dew RUNTIME DESTINATION "${DEW_RUNTIME_DIR}" COMPONENT dew)
  install(FILES ${DEW_STAGED_DOCS} DESTINATION "${DEW_DOC_DIR}" COMPONENT dew)
endif()

# ------------------------------------------------------------------------------
# CPack.
# ------------------------------------------------------------------------------
# ONE component, named, and every install() above joins it.
#
# Not tidiness: JUCE and Catch2 are added by CPM as ordinary subprojects and
# bring their own install() rules, so a monolithic pack put bin/, include/ and
# lib/ beside dew.app in the first .dmg this produced. Naming the component dew
# leaves everything they install in the Unspecified one, which CPack then does
# not pack.
set(CPACK_COMPONENTS_ALL dew)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)

set(CPACK_PACKAGE_NAME "dew")
set(CPACK_PACKAGE_VENDOR "dew")
set(CPACK_PACKAGE_VERSION "${DEW_PACKAGE_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A desktop digital synth DAW")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/janschupke/dew")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "dew")

# No source package. `cpack --config CPackSourceConfig.cmake` on a tree holding
# build/ and node_modules/ produces a tarball nobody wants, and GitHub already
# attaches the git archive to every release.
set(CPACK_SOURCE_GENERATOR "")

if(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_PACKAGE_FILE_NAME "dew-macos-universal")
  set(CPACK_DMG_VOLUME_NAME "dew ${DEW_PACKAGE_VERSION}")
elseif(WIN32)
  set(CPACK_GENERATOR "INNOSETUP;ZIP")
  set(CPACK_PACKAGE_FILE_NAME "dew-windows-x64")

  # lowest, so installing raises no UAC prompt at all. An unsigned installer
  # that also asks for administrator gets two alarming dialogs where it could
  # have had one, and dew has nothing to write outside the user's own profile:
  # {autopf} resolves to %LOCALAPPDATA%\Programs under this setting.
  set(CPACK_INNOSETUP_SETUP_PrivilegesRequired "lowest")
  set(CPACK_INNOSETUP_SETUP_AppPublisher "dew")
  set(CPACK_INNOSETUP_SETUP_AppPublisherURL "${CPACK_PACKAGE_HOMEPAGE_URL}")
  set(CPACK_INNOSETUP_SETUP_ArchitecturesAllowed "x64compatible")
  set(CPACK_INNOSETUP_SETUP_ArchitecturesInstallIn64BitMode "x64compatible")
  set(CPACK_INNOSETUP_CREATE_UNINSTALL_LINK ON)
else()
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_PACKAGE_FILE_NAME "dew-linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()

include(CPack)
