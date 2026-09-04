# ==============================================================================
# GenStrings.cmake - the string catalogue, from resources/i18n/*.json
#
# Run in script mode by an add_custom_command in src/CMakeLists.txt:
#
#     cmake -DDEW_I18N_LOCALES=... -DDEW_I18N_DIR=... -DDEW_I18N_OUT=...
#           -P cmake/GenStrings.cmake
#
# Emits, for dew_i18n, StringIds.h (an enum, one enumerator per key) and
# Catalogs.cpp (the key paths, the text, and the argument names each message
# expects) - and, for dew_lang, the same pair as MessageIds.h and
# MessageCatalogs.cpp over the `lang.` subtree alone.
#
# Two artefacts from one authored file, because the two libraries cannot share
# one. dew_lang links NOTHING, JUCE included, so it cannot hold a juce::String;
# dew_i18n links juce_core, so its catalogue is juce::String all the way down.
# A translator still sees a single en.json, which is the point of splitting here
# rather than there.
#
# PluralTable.h is emitted into BOTH, byte for byte identical, from
# resources/i18n/plurals.txt. See that file for why it is duplicated at all.
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

foreach(required DEW_I18N_LOCALES DEW_I18N_DIR DEW_I18N_OUT DEW_LANG_OUT)
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
# One term of a plural condition as a C++ expression.
#
# "n % 100 != 12..14" -> "! (n % 100 >= 12 && n % 100 <= 14)". The shape check
# is the whole safety argument: a condition this regex does not recognise stops
# the build rather than emitting a rule that quietly matches nothing.
# ------------------------------------------------------------------------------
function(dew_plural_term term out)
  if(NOT term MATCHES "^n( % ([0-9]+))? (=|!=|<=) ([0-9]+)(\\.\\.([0-9]+))?$")
    message(FATAL_ERROR
      "plurals.txt: '${term}' is not a condition. One term is "
      "'n [% <m>] (= | != | <=) <value>[..<value>]', and the header of that "
      "file says why the vocabulary is that small.")
  endif()

  set(modulus "${CMAKE_MATCH_2}")
  set(operator "${CMAKE_MATCH_3}")
  set(low "${CMAKE_MATCH_4}")
  set(high "${CMAKE_MATCH_6}")

  if(modulus STREQUAL "")
    set(lhs "n")
  else()
    set(lhs "n % ${modulus}")
  endif()

  if(high STREQUAL "")
    if(operator STREQUAL "=")
      set(expression "${lhs} == ${low}")
    elseif(operator STREQUAL "!=")
      set(expression "${lhs} != ${low}")
    else()
      set(expression "${lhs} <= ${low}")
    endif()
  else()
    if(operator STREQUAL "<=")
      message(FATAL_ERROR "plurals.txt: '${term}' - a range cannot take <=")
    endif()

    set(expression "${lhs} >= ${low} && ${lhs} <= ${high}")

    # >= and <= bind tighter than &&, so a range needs no parentheses of its
    # own where it is the whole condition - only where it is negated, or where
    # dew_plural_condition joins it to another term.
    if(operator STREQUAL "!=")
      set(expression "! (${expression})")
    endif()
  endif()

  set(${out} "${expression}" PARENT_SCOPE)
endfunction()

function(dew_plural_condition condition out)
  string(REPLACE " and " ";" terms "${condition}")
  set(expression "")

  foreach(term ${terms})
    string(STRIP "${term}" term)
    dew_plural_term("${term}" rendered)

    if(expression STREQUAL "")
      set(expression "${rendered}")
    else()
      if(NOT expression MATCHES "^[!(]")
        set(expression "(${expression})")
      endif()

      if(NOT rendered MATCHES "^[!(]")
        set(rendered "(${rendered})")
      endif()

      string(APPEND expression " && ${rendered}")
    endif()
  endforeach()

  set(${out} "${expression}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# PluralTable.h, from resources/i18n/plurals.txt.
# ------------------------------------------------------------------------------
set(plurals_file "${DEW_I18N_DIR}/plurals.txt")

if(NOT EXISTS "${plurals_file}")
  message(FATAL_ERROR "GenStrings.cmake: ${plurals_file} does not exist")
endif()

file(STRINGS "${plurals_file}" plural_lines)

set(rules "")           # the body of categoryFor, accumulated
set(open_family "")     # the family being read, or "" between blocks
set(default_families 0) # how many said `languages *`
set(family_count 0)

# A trailing `endif()` for the family in progress, so a block closes when the
# next one opens rather than every branch remembering to close itself.
macro(dew_plural_close)
  if(NOT open_family STREQUAL "" AND NOT open_family STREQUAL "*")
    string(APPEND rules "    }\n")
  endif()

  set(open_family "")
endmacro()

foreach(line ${plural_lines})
  string(STRIP "${line}" line)

  if(line STREQUAL "" OR line MATCHES "^#")
    continue()
  endif()

  string(REGEX REPLACE "^([^ ]+) *(.*)$" "\\1" head "${line}")
  string(REGEX REPLACE "^([^ ]+) *(.*)$" "\\2" tail "${line}")

  if(head STREQUAL "family")
    dew_plural_close()
    set(pending_family "${tail}")
    math(EXPR family_count "${family_count} + 1")
  elseif(head STREQUAL "languages")
    if(pending_family STREQUAL "")
      message(FATAL_ERROR "plurals.txt: 'languages ${tail}' before any 'family'")
    endif()

    string(APPEND rules "\n    // ${pending_family}\n")

    if(tail STREQUAL "*")
      set(open_family "*")
      math(EXPR default_families "${default_families} + 1")
    else()
      string(REPLACE " " ";" subtags "${tail}")
      set(test "")

      foreach(subtag ${subtags})
        if(NOT subtag MATCHES "^[a-z][a-z][a-z]?$")
          message(FATAL_ERROR "plurals.txt: '${subtag}' is not a language subtag")
        endif()

        if(test STREQUAL "")
          set(test "isLanguage (locale, \"${subtag}\")")
        else()
          # Wrapped where clang-format would wrap it. The generated headers are
          # not run through the formatter - they are not under src/ - so what is
          # emitted is what is read.
          string(APPEND test "\n        || isLanguage (locale, \"${subtag}\")")
        endif()
      endforeach()

      string(APPEND rules "    if (${test})\n    {\n")
      set(open_family "${pending_family}")
    endif()

    set(pending_family "")
  else()
    if(open_family STREQUAL "")
      message(FATAL_ERROR "plurals.txt: '${line}' is outside a family, or before its languages")
    endif()

    if(NOT head MATCHES "^(zero|one|two|few|many|other)$")
      message(FATAL_ERROR "plurals.txt: '${head}' is not a CLDR plural category")
    endif()

    if(open_family STREQUAL "*")
      set(indent "    ")
    else()
      set(indent "        ")
    endif()

    if(tail STREQUAL "always")
      string(APPEND rules "${indent}return Category::${head};\n")
    else()
      dew_plural_condition("${tail}" expression)
      string(APPEND rules "${indent}if (${expression})\n${indent}    return Category::${head};\n\n")
    endif()
  endif()
endforeach()

dew_plural_close()

if(NOT default_families EQUAL 1)
  message(FATAL_ERROR
    "plurals.txt: ${default_families} families say 'languages *'. Exactly one has to: "
    "it is the rule every language the file does not name falls back to, and two of "
    "them would mean the second is dead.")
endif()

set(plural "// Generated by cmake/GenStrings.cmake from resources/i18n/plurals.txt.\n")
string(APPEND plural "// Do not edit.\n")
string(APPEND plural "//\n")
string(APPEND plural "// Emitted into dew_i18n's and dew_lang's generated directories, byte for\n")
string(APPEND plural "// byte identical, because neither library may include the other's header.\n")
string(APPEND plural "// A test compares the two copies; plurals.txt argues the rest.\n\n")
string(APPEND plural "#pragma once\n\n")
string(APPEND plural "#include <cstdint>\n#include <string_view>\n\n")
string(APPEND plural "namespace dew::plural\n{\n\n")
string(APPEND plural "/** CLDR's plural categories.\n\n")
string(APPEND plural "    Six, because that is how many the widest locale needs, not because\n")
string(APPEND plural "    English needs them. English selects between two and Czech between four,\n")
string(APPEND plural "    and a message that names 'one' and 'other' is answered correctly in\n")
string(APPEND plural "    both - the rule picks a category, and a branch the message does not\n")
string(APPEND plural "    carry falls back to 'other'.\n*/\n")
string(APPEND plural "enum class Category\n{\n")
string(APPEND plural "    zero,\n    one,\n    two,\n    few,\n    many,\n    other\n};\n\n")
string(APPEND plural "inline constexpr int numCategories = 6;\n\n")
string(APPEND plural "/** The categories in enum order, spelled as a plural branch names them. */\n")
string(APPEND plural "inline constexpr const char* categoryNames[] {\n")
string(APPEND plural "    \"zero\", \"one\", \"two\", \"few\", \"many\", \"other\",\n};\n\n")
string(APPEND plural "/** Whether `locale`'s language subtag is `language`.\n\n")
string(APPEND plural "    \"fr-CA\" is fr. Plural rules are a property of the language and never\n")
string(APPEND plural "    of the region: there is no locale where a country changes how a number\n")
string(APPEND plural "    is counted, and treating \"fr-CA\" as unknown would silently give it\n")
string(APPEND plural "    English's rule.\n*/\n")
string(APPEND plural "inline constexpr bool isLanguage (std::string_view locale,\n")
string(APPEND plural "                                 std::string_view language) noexcept\n{\n")
string(APPEND plural "    if (locale.size() < language.size())\n        return false;\n\n")
string(APPEND plural "    if (locale.size() > language.size() && locale[language.size()] != '-')\n")
string(APPEND plural "        return false;\n\n")
string(APPEND plural "    for (std::size_t i = 0; i < language.size(); ++i)\n    {\n")
string(APPEND plural "        auto c = locale[i];\n\n")
string(APPEND plural "        if (c >= 'A' && c <= 'Z')\n")
string(APPEND plural "            c = (char) (c - 'A' + 'a');\n\n")
string(APPEND plural "        if (c != language[i])\n            return false;\n    }\n\n")
string(APPEND plural "    return true;\n}\n\n")
string(APPEND plural "/** The category `count` takes in `locale`. */\n")
string(APPEND plural "inline constexpr Category categoryFor (std::string_view locale,\n")
string(APPEND plural "                                      std::int64_t count) noexcept\n{\n")
string(APPEND plural "    const auto n = count < 0 ? -count : count;\n")
string(APPEND plural "${rules}}\n\n")
string(APPEND plural "inline constexpr const char* nameOfCategory (Category category) noexcept\n{\n")
string(APPEND plural "    const auto index = (int) category;\n\n")
string(APPEND plural "    return index >= 0 && index < numCategories ? categoryNames[index] : \"other\";\n}\n\n")
string(APPEND plural "inline constexpr bool isCategoryName (std::string_view name) noexcept\n{\n")
string(APPEND plural "    for (const auto* candidate : categoryNames)\n")
string(APPEND plural "        if (name == candidate)\n            return true;\n\n")
string(APPEND plural "    return false;\n}\n\n")
string(APPEND plural "inline constexpr Category categoryNamed (std::string_view name) noexcept\n{\n")
string(APPEND plural "    for (int i = 0; i < numCategories; ++i)\n")
string(APPEND plural "        if (name == categoryNames[i])\n            return (Category) i;\n\n")
string(APPEND plural "    return Category::other;\n}\n\n")
string(APPEND plural "} // namespace dew::plural\n")

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

# ------------------------------------------------------------------------------
# Split. The `lang.` subtree is the score language's and reaches dew_lang; every
# other key is the application's and reaches dew_i18n.
#
# One authored file and two artefacts, rather than two files. A translator opens
# en.json and sees every sentence dew can say; which of dew's libraries holds a
# given one is a fact about the link graph and no business of theirs.
#
# The prefix is stripped from the enumerator - Msg::generatorTooManyChannels,
# not Msg::langGeneratorTooManyChannels - because Msg already lives in
# namespace dew::lang. It is kept in the key PATH, which is what a missing row
# falls back to and what the orphan gate compares against.
# ------------------------------------------------------------------------------
set(app_keys "")
set(lang_keys "")

foreach(key ${keys})
  if(key MATCHES "^lang\\.")
    list(APPEND lang_keys "${key}")
  else()
    list(APPEND app_keys "${key}")
  endif()
endforeach()

if(lang_keys STREQUAL "")
  message(FATAL_ERROR
    "GenStrings.cmake: ${DEW_I18N_REFERENCE}.json declares no 'lang.' keys. The "
    "score language's catalogue would be an empty enum and a zero-length array, "
    "which is not valid C++ and not a catalogue either.")
endif()

set(keys ${app_keys})
list(LENGTH keys key_count)
list(LENGTH lang_keys lang_key_count)

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
# MessageIds.h - dew_lang's half of the enum.
#
# std::string_view rather than juce::String, and no include of anything dew
# owns, so this compiles in a library that links nothing at all.
# ------------------------------------------------------------------------------
set(ids_lang "// Generated by cmake/GenStrings.cmake. Do not edit.\n")
string(APPEND ids_lang "//\n")
string(APPEND ids_lang "// One enumerator per `lang.` key in resources/i18n/${DEW_I18N_REFERENCE}.json,\n")
string(APPEND ids_lang "// with the prefix stripped: Msg already lives in namespace dew::lang.\n\n")
string(APPEND ids_lang "#pragma once\n\nnamespace dew::lang\n{\n\n")
string(APPEND ids_lang "/** A sentence the score language can say.\n\n")
string(APPEND ids_lang "    Named per CALL SITE and not per diagnostic code, because the codes are\n")
string(APPEND ids_lang "    many-to-many with the wording: E103 is one error reported by two parsers,\n")
string(APPEND ids_lang "    and \"no channel called `x`\" is reported under three different codes. A\n")
string(APPEND ids_lang "    code stays what Diagnostic::code has always been.\n*/\n")
string(APPEND ids_lang "enum class Msg\n{\n")

foreach(key ${lang_keys})
  string(REGEX REPLACE "^lang\\." "" bare "${key}")
  dew_i18n_mangle("${bare}" enumerator)
  string(APPEND ids_lang "    ${enumerator},\n")
endforeach()

string(APPEND ids_lang "};\n\n")
string(APPEND ids_lang "inline constexpr int numMessages = ${lang_key_count};\n\n")
string(APPEND ids_lang "/** The locales this build carries, in the order they were configured.\n\n")
string(APPEND ids_lang "    An enum rather than a tag, and passed rather than held: dew_lang has no\n")
string(APPEND ids_lang "    mutable global state at all - only const tables - and the library whose\n")
string(APPEND ids_lang "    whole claim is determinism is the last one that should gain its first.\n*/\n")
string(APPEND ids_lang "enum class Locale\n{\n")

foreach(locale ${DEW_I18N_LOCALES})
  string(MAKE_C_IDENTIFIER "${locale}" locale_identifier)
  string(APPEND ids_lang "    ${locale_identifier},\n")
endforeach()

string(APPEND ids_lang "};\n\n")
string(APPEND ids_lang "inline constexpr int numLocales = ${locale_count};\n")
string(MAKE_C_IDENTIFIER "${DEW_I18N_REFERENCE}" reference_identifier)
string(APPEND ids_lang "inline constexpr Locale referenceLocale = Locale::${reference_identifier};\n\n")
string(APPEND ids_lang "} // namespace dew::lang\n")

# ------------------------------------------------------------------------------
# MessageCatalogs.cpp
# ------------------------------------------------------------------------------
set(cat_lang "// Generated by cmake/GenStrings.cmake. Do not edit.\n\n")
string(APPEND cat_lang "#include \"lang/MessageCatalog.h\"\n\nnamespace dew::lang\n{\n\n")
string(APPEND cat_lang "namespace\n{\n\n")

string(APPEND cat_lang "const char* const keyPaths[] {\n")

foreach(key ${lang_keys})
  string(APPEND cat_lang "    \"${key}\",\n")
endforeach()

string(APPEND cat_lang "};\n\n")

string(APPEND cat_lang "const char* const argumentNames[] {\n")

foreach(key ${lang_keys})
  get_property(value GLOBAL PROPERTY "dew_i18n_${DEW_I18N_REFERENCE}_${key}")
  dew_i18n_argument_names("${value}" names)
  dew_i18n_escape("${names}" escaped_names)
  string(APPEND cat_lang "    \"${escaped_names}\",\n")
endforeach()

string(APPEND cat_lang "};\n\n")

foreach(locale ${DEW_I18N_LOCALES})
  string(MAKE_C_IDENTIFIER "${locale}" locale_identifier)
  string(APPEND cat_lang "const char* const text_${locale_identifier}[] {\n")

  foreach(key ${lang_keys})
    get_property(value GLOBAL PROPERTY "dew_i18n_${locale}_${key}")
    dew_i18n_escape("${value}" escaped)
    string(APPEND cat_lang "    \"${escaped}\",\n")
  endforeach()

  string(APPEND cat_lang "};\n\n")
endforeach()

string(APPEND cat_lang "const char* const* const localeText[] {\n")

foreach(locale ${DEW_I18N_LOCALES})
  string(MAKE_C_IDENTIFIER "${locale}" locale_identifier)
  string(APPEND cat_lang "    text_${locale_identifier},\n")
endforeach()

string(APPEND cat_lang "};\n\n")

string(APPEND cat_lang "const char* const localeTags[] {\n")

foreach(locale ${DEW_I18N_LOCALES})
  string(APPEND cat_lang "    \"${locale}\",\n")
endforeach()

string(APPEND cat_lang "};\n\n")
string(APPEND cat_lang "bool inRange (Msg id) noexcept\n{\n")
string(APPEND cat_lang "    return (int) id >= 0 && (int) id < numMessages;\n}\n\n")
string(APPEND cat_lang "} // namespace\n\n")

string(APPEND cat_lang "std::string_view messageText (Msg id, Locale locale) noexcept\n{\n")
string(APPEND cat_lang "    if (! inRange (id))\n        return {};\n\n")
string(APPEND cat_lang "    const auto index = (int) locale;\n")
string(APPEND cat_lang "    const auto* text = index >= 0 && index < numLocales\n")
string(APPEND cat_lang "                           ? localeText[index][(int) id]\n")
string(APPEND cat_lang "                           : \"\";\n\n")
string(APPEND cat_lang "    // A row this locale does not carry falls back to the reference, and a\n")
string(APPEND cat_lang "    // reference row that is empty falls back to the key. tr() answers a\n")
string(APPEND cat_lang "    // missing string the same way and for the same reason: a diagnostic with\n")
string(APPEND cat_lang "    // no text at all reads as a compiler that found something and would not\n")
string(APPEND cat_lang "    // say what.\n")
string(APPEND cat_lang "    if (text[0] == '\\0')\n")
string(APPEND cat_lang "        text = localeText[(int) referenceLocale][(int) id];\n\n")
string(APPEND cat_lang "    return text[0] == '\\0' ? keyPaths[(int) id] : text;\n}\n\n")

string(APPEND cat_lang "std::string_view messageKey (Msg id) noexcept\n{\n")
string(APPEND cat_lang "    return inRange (id) ? keyPaths[(int) id] : std::string_view {};\n}\n\n")

string(APPEND cat_lang "std::string_view messageArgumentNames (Msg id) noexcept\n{\n")
string(APPEND cat_lang "    return inRange (id) ? argumentNames[(int) id] : std::string_view {};\n}\n\n")

string(APPEND cat_lang "std::string_view localeTag (Locale locale) noexcept\n{\n")
string(APPEND cat_lang "    const auto index = (int) locale;\n\n")
string(APPEND cat_lang "    return index >= 0 && index < numLocales ? localeTags[index]\n")
string(APPEND cat_lang "                                           : localeTags[(int) referenceLocale];\n}\n\n")

string(APPEND cat_lang "Locale localeFor (std::string_view tag) noexcept\n{\n")
string(APPEND cat_lang "    for (int i = 0; i < numLocales; ++i)\n")
string(APPEND cat_lang "        if (tag == localeTags[i])\n")
string(APPEND cat_lang "            return (Locale) i;\n\n")
string(APPEND cat_lang "    // The language alone, so \"de-CH\" reaches \"de\". The application already\n")
string(APPEND cat_lang "    // narrows its own tag this way; doing it here as well is what keeps a\n")
string(APPEND cat_lang "    // regional tag from silently compiling its errors in English.\n")
string(APPEND cat_lang "    for (int i = 0; i < numLocales; ++i)\n")
string(APPEND cat_lang "        if (plural::isLanguage (tag, localeTags[i]))\n")
string(APPEND cat_lang "            return (Locale) i;\n\n")
string(APPEND cat_lang "    return referenceLocale;\n}\n\n")
string(APPEND cat_lang "} // namespace dew::lang\n")

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

file(MAKE_DIRECTORY "${DEW_LANG_OUT}")
dew_i18n_write("${DEW_LANG_OUT}/MessageIds.h" "${ids_lang}")
dew_i18n_write("${DEW_LANG_OUT}/MessageCatalogs.cpp" "${cat_lang}")

# The same bytes in both places, which is what the test that compares them
# checks and what makes the duplication a fact rather than a hope.
dew_i18n_write("${DEW_I18N_OUT}/PluralTable.h" "${plural}")
dew_i18n_write("${DEW_LANG_OUT}/PluralTable.h" "${plural}")
