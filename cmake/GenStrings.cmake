# ==============================================================================
# GenStrings.cmake - the string catalogue, from resources/i18n/*.json
#
# Run in script mode by an add_custom_command in src/CMakeLists.txt:
#
#     cmake -DDEW_I18N_LOCALES=... -DDEW_I18N_DIR=... -DDEW_I18N_OUT=...
#           -P cmake/GenStrings.cmake
#
# Emits StringIds.h (an enum, one enumerator per key) and Catalogs.cpp (the key
# paths, the text, and the argument names each message expects).
#
# CMake rather than a script in another language, because string(JSON) has been
# in CMake since 3.19 and this project already requires 3.25. A generator with
# no interpreter behind it is one that cannot be the reason a clean checkout
# fails to build.
#
# The output goes to the BUILD tree and is never committed. Under src/ it would
# be subject to the gate on file length, and a catalogue is longer than any
# rule about hand-written code should have an opinion about.
# ==============================================================================

cmake_minimum_required(VERSION 3.25)

foreach(required DEW_I18N_LOCALES DEW_I18N_DIR DEW_I18N_OUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "GenStrings.cmake: -D${required} is required")
  endif()
endforeach()

# ------------------------------------------------------------------------------
# The walk.
#
# Keys accumulate in a GLOBAL property rather than travelling back through
# PARENT_SCOPE. The recursion goes as deep as the JSON does, and returning a
# list up N scopes is N places for a nesting bug to hide and none of them to
# report it.
# ------------------------------------------------------------------------------
set_property(GLOBAL PROPERTY dew_i18n_keys "")

function(dew_i18n_walk json prefix locale)
  string(JSON member_count LENGTH "${json}")

  if(member_count EQUAL 0)
    return()
  endif()

  math(EXPR last "${member_count} - 1")

  foreach(i RANGE ${last})
    string(JSON member MEMBER "${json}" ${i})
    string(JSON member_type TYPE "${json}" "${member}")

    if(prefix STREQUAL "")
      set(key "${member}")
    else()
      set(key "${prefix}.${member}")
    endif()

    if(member_type STREQUAL OBJECT)
      string(JSON subtree GET "${json}" "${member}")
      dew_i18n_walk("${subtree}" "${key}" "${locale}")
    elseif(member_type STREQUAL STRING)
      string(JSON value GET "${json}" "${member}")

      if(locale STREQUAL "${DEW_I18N_REFERENCE}")
        set_property(GLOBAL APPEND PROPERTY dew_i18n_keys "${key}")
      endif()

      set_property(GLOBAL PROPERTY "dew_i18n_${locale}_${key}" "${value}")
    else()
      message(FATAL_ERROR
        "${locale}.json: '${key}' is a ${member_type}. A catalogue holds objects and "
        "strings and nothing else - a number or a list here is a key that no lookup "
        "can reach.")
    endif()
  endforeach()
endfunction()

# ------------------------------------------------------------------------------
# A dotted key to a C++ enumerator.
#
# The shape check is the whole collision argument. A key segment may not contain
# an underscore, so '.' -> '_' is injective and two different keys cannot mangle
# to one enumerator. That is why there is a regex here and no collision table
# anywhere.
# ------------------------------------------------------------------------------
function(dew_i18n_mangle key out)
  if(NOT key MATCHES "^[a-z][a-zA-Z0-9]*(\\.[a-z][a-zA-Z0-9]*)*$")
    message(FATAL_ERROR
      "i18n key '${key}': lowerCamel segments joined by dots, and nothing else. "
      "An underscore is banned so that '.' -> '_' cannot collide; that is the "
      "reason this check exists and the reason nothing downstream checks again.")
  endif()

  string(REPLACE "." "_" mangled "${key}")
  set(${out} "${mangled}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# A catalogue value as a C++ string literal. Backslash first, or every escape
# this adds is escaped again by the next replacement.
# ------------------------------------------------------------------------------
function(dew_i18n_escape value out)
  string(REPLACE "\\" "\\\\" escaped "${value}")
  string(REPLACE "\"" "\\\"" escaped "${escaped}")
  string(REPLACE "\n" "\\n" escaped "${escaped}")
  set(${out} "${escaped}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# The argument names a message asks for, so a test can answer a message without
# knowing what it says. "{count, plural, ...}" and "{name}" both name their
# argument first, so one pattern finds every one of them.
# ------------------------------------------------------------------------------
function(dew_i18n_argument_names value out)
  set(names "")
  string(REGEX MATCHALL "{[ \t]*[a-z][a-zA-Z0-9]*" opens "${value}")

  foreach(opened ${opens})
    string(REGEX REPLACE "^{[ \t]*" "" name "${opened}")

    # 'one', 'other', 'few' and the rest open a plural BRANCH, not an argument.
    if(NOT name MATCHES "^(zero|one|two|few|many|other)$")
      list(APPEND names "${name}")
    endif()
  endforeach()

  list(REMOVE_DUPLICATES names)
  string(REPLACE ";" " " joined "${names}")
  set(${out} "${joined}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# Read every locale. The first is the reference: it declares the key set, and
# every other locale is a partial overlay on it.
# ------------------------------------------------------------------------------
list(GET DEW_I18N_LOCALES 0 DEW_I18N_REFERENCE)

foreach(locale ${DEW_I18N_LOCALES})
  set(catalogue "${DEW_I18N_DIR}/${locale}.json")

  if(NOT EXISTS "${catalogue}")
    message(FATAL_ERROR "GenStrings.cmake: ${catalogue} does not exist")
  endif()

  file(READ "${catalogue}" contents)
  dew_i18n_walk("${contents}" "" "${locale}")
endforeach()

get_property(keys GLOBAL PROPERTY dew_i18n_keys)

if(keys STREQUAL "")
  message(FATAL_ERROR "GenStrings.cmake: ${DEW_I18N_REFERENCE}.json declares no keys")
endif()

# Sorted, so the enum is a function of the key SET rather than of the order
# somebody happened to type them in. Reordering en.json then rebuilds nothing.
list(SORT keys)
list(LENGTH keys key_count)

# ------------------------------------------------------------------------------
# StringIds.h
# ------------------------------------------------------------------------------
set(ids "// Generated by cmake/GenStrings.cmake. Do not edit.\n")
string(APPEND ids "//\n")
string(APPEND ids "// One enumerator per key in resources/i18n/${DEW_I18N_REFERENCE}.json.\n")
string(APPEND ids "// A key the app names and the catalogue does not hold is a compile\n")
string(APPEND ids "// error, which is the whole reason the enum is generated from it.\n\n")
string(APPEND ids "#pragma once\n\nnamespace dew\n{\n\n")
string(APPEND ids "enum class StringId\n{\n")

foreach(key ${keys})
  dew_i18n_mangle("${key}" enumerator)
  string(APPEND ids "    ${enumerator},\n")
endforeach()

string(APPEND ids "};\n\n")
string(APPEND ids "inline constexpr int numStrings = ${key_count};\n\n")
string(APPEND ids "} // namespace dew\n")

# ------------------------------------------------------------------------------
# Catalogs.cpp
# ------------------------------------------------------------------------------
set(cat "// Generated by cmake/GenStrings.cmake. Do not edit.\n\n")
string(APPEND cat "#include \"i18n/Catalog.h\"\n\nnamespace dew\n{\n\n")
string(APPEND cat "namespace\n{\n\n")

string(APPEND cat "const char* const keyPaths[] {\n")

foreach(key ${keys})
  string(APPEND cat "    \"${key}\",\n")
endforeach()

string(APPEND cat "};\n\n")

string(APPEND cat "const char* const argumentNames[] {\n")

foreach(key ${keys})
  get_property(value GLOBAL PROPERTY "dew_i18n_${DEW_I18N_REFERENCE}_${key}")
  dew_i18n_argument_names("${value}" names)
  dew_i18n_escape("${names}" escaped_names)
  string(APPEND cat "    \"${escaped_names}\",\n")
endforeach()

string(APPEND cat "};\n\n")

foreach(locale ${DEW_I18N_LOCALES})
  string(MAKE_C_IDENTIFIER "${locale}" locale_identifier)
  string(APPEND cat "const char* const text_${locale_identifier}[] {\n")

  foreach(key ${keys})
    get_property(value GLOBAL PROPERTY "dew_i18n_${locale}_${key}")

    # A key the reference declares and this locale does not is written as an
    # empty row. Strings.cpp falls back to the reference for those, so a
    # partial translation is a partial translation rather than a blank UI.
    dew_i18n_escape("${value}" escaped)
    string(APPEND cat "    \"${escaped}\",\n")
  endforeach()

  string(APPEND cat "};\n\n")
endforeach()

string(APPEND cat "} // namespace\n\n")

string(APPEND cat "const Catalog* catalogs() noexcept\n{\n")
string(APPEND cat "    static const Catalog table[] {\n")

foreach(locale ${DEW_I18N_LOCALES})
  string(MAKE_C_IDENTIFIER "${locale}" locale_identifier)
  string(APPEND cat "        { \"${locale}\", text_${locale_identifier} },\n")
endforeach()

string(APPEND cat "    };\n\n    return table;\n}\n\n")

list(LENGTH DEW_I18N_LOCALES locale_count)
string(APPEND cat "int numCatalogs() noexcept\n{\n    return ${locale_count};\n}\n\n")
string(APPEND cat "const char* const* catalogKeyPaths() noexcept\n{\n    return keyPaths;\n}\n\n")
string(APPEND cat "const char* const* catalogArgumentNames() noexcept\n{\n")
string(APPEND cat "    return argumentNames;\n}\n\n")
string(APPEND cat "} // namespace dew\n")

# ------------------------------------------------------------------------------
# Written only when they changed. Ninja reruns this command whenever en.json is
# newer than its outputs; rewriting identical bytes would then rebuild every
# translation unit that includes StringIds.h for no reason at all.
# ------------------------------------------------------------------------------
function(dew_i18n_write path contents)
  if(EXISTS "${path}")
    file(READ "${path}" existing)

    if(existing STREQUAL "${contents}")
      file(TOUCH "${path}")
      return()
    endif()
  endif()

  file(WRITE "${path}" "${contents}")
endfunction()

file(MAKE_DIRECTORY "${DEW_I18N_OUT}")
dew_i18n_write("${DEW_I18N_OUT}/StringIds.h" "${ids}")
dew_i18n_write("${DEW_I18N_OUT}/Catalogs.cpp" "${cat}")
