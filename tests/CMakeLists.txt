# ==============================================================================
# dew_tests - Catch2, registered with CTest so `ctest` is the single command
# that runs everything.
# ==============================================================================

add_executable(dew_tests
  BuildInfoTests.cpp
  DocumentIdentityTests.cpp
  DemoLibraryTests.cpp
  FixtureProject.cpp
  FixtureProjectTests.cpp
  DesignSystemTests.cpp
  SchemaTests.cpp
  SettingsTests.cpp
  SelectionTests.cpp
  StatusBarTests.cpp
  ProjectEditsTests.cpp
  PresetTests.cpp
  NoteToolsTests.cpp
  TempoMapTests.cpp
  TransportTests.cpp
  SequencerTests.cpp
  LoopRegionTests.cpp
  StepGridTests.cpp
  ChannelRackTests.cpp
  ConfirmTests.cpp
  PreviewQueueTests.cpp
  SignalTapTests.cpp
  SnapshotBridgeTests.cpp
  AudioSettingsTests.cpp
  MidiRouterTests.cpp
  MidiInputTests.cpp
  PitchBendTests.cpp
  AutomationLaneTests.cpp
  AutomationTests.cpp
  EffectTests.cpp
  EngineTests.cpp
  MixerTests.cpp
  OscillatorTests.cpp
  WavetableTests.cpp
  TimelineViewTests.cpp
  ScrollSpeedTests.cpp
  EntityColourTests.cpp
  HoverHelpTests.cpp
  TimelineRulerTests.cpp
  TypographyTests.cpp
  PianoRollTests.cpp
  PianoRollToolsTests.cpp
  ParamMenuTests.cpp
  PlaylistTests.cpp
  MeterTests.cpp
  MidiExportTests.cpp
  RenderFormatTests.cpp
  RenderJobTests.cpp
  RenderPanelTests.cpp
  RenderPostTests.cpp
  RenderStemTests.cpp
  RenderRangeTests.cpp
  UiSmokeTests.cpp
  SignalScopeTests.cpp
  WaveformPeaksTests.cpp
  AudioClipTests.cpp
  SamplePlayerTests.cpp
  FixtureSoundFont.cpp
  SoundFontCorpusTests.cpp
  SoundFontFileTests.cpp
  SoundFontTests.cpp
  AssetPathTests.cpp
  AudioRecorderTests.cpp
  AudioUiTests.cpp
  CliArgsTests.cpp
  GestureTests.cpp
  HotkeyTests.cpp
  SourceGateTests.cpp
  RealtimeTests.cpp
  AllocationGateTests.cpp
  CatalogTests.cpp
  MotionTests.cpp
  ScoreLexerTests.cpp
  ScoreGridTests.cpp
  ScoreDiagnosticTests.cpp
  ScoreMusicTests.cpp
  ScoreParserTests.cpp
  ScoreResolverTests.cpp
  ScoreGeneratorTests.cpp
  ScoreCounterpointTests.cpp
  ScoreImitationTests.cpp
  ScoreBakeTests.cpp
  ScoreSchemaTests.cpp
  ScoreEditorTests.cpp
  ScoreCompletionTests.cpp
  ScoreDeterminismTests.cpp
)

target_link_libraries(dew_tests
  PRIVATE
    dew_ui   # and dew_lang with it: dew_model links it PUBLIC, and naming it
             # again here made every link warn about a duplicate library
    Catch2::Catch2WithMain
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# The example project is committed, so a test can check it stays loadable and
# in step with the factory that generates it.
# The source gates scan for raw font construction, relative includes and more,
# so they need to know where the sources are. Same idiom as examples above.
target_compile_definitions(dew_tests PRIVATE
  DEW_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
    DEW_PRESETS_DIR="${CMAKE_SOURCE_DIR}/presets"
  DEW_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src")

# What is actually COMPILED, as opposed to what happens to sit under src/.
#
# Every source gate walks DEW_SOURCE_DIR, and every one of them passes silently
# when it finds nothing - which is indistinguishable from finding nothing
# because the code moved. Handing the suite the libraries' own SOURCES lets it
# check the walk against the truth: a file that joins a library and not the walk
# fails here rather than quietly falling out of every other gate.
# Written to a file rather than squeezed through a -D: the list is sixty-odd
# paths and its natural separator is the semicolon, which is also CMake's list
# separator, so passing it as a definition turns each path into its own -D flag.
#
# Each line is "<layer> <source>", not just the source. The layering gate needs
# to know which library a file belongs to, and CMake is the only thing that
# does: a directory is a convention, but target SOURCES is the truth. Without
# the label the gate would have to infer the layer from the path, which is the
# very assumption that stopped holding when dew_design took four files that
# still live under src/ui.
set(DEW_GATE_SOURCES "")
foreach(dew_layer dew_lang dew_model dew_engine dew_io dew_design dew_app dew_ui)
  # The layer name leads every line: once before the JOIN for the first source,
  # and once inside the separator for each of the rest.
  list(APPEND DEW_GATE_SOURCES
       "${dew_layer} $<JOIN:$<TARGET_PROPERTY:${dew_layer},SOURCES>,\n${dew_layer} >")
endforeach()

file(GENERATE
  OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/compiled-sources.txt"
  CONTENT "$<JOIN:${DEW_GATE_SOURCES},\n>")

target_compile_definitions(dew_tests PRIVATE
  DEW_COMPILED_SOURCES_FILE="${CMAKE_CURRENT_BINARY_DIR}/compiled-sources.txt")

# The console tools' shared argument parser is header-only and lives beside them,
# so the suite can test it without either tool being on the link line.
target_include_directories(dew_tests PRIVATE "${CMAKE_SOURCE_DIR}/tools")

catch_discover_tests(dew_tests)

# ==============================================================================
# dew_lang_tests - the score language on its own.
#
# The eleven files below include nothing but lang/ headers and name juce:: not
# once, so this target links dew_lang and Catch2 and no JUCE at all. That is
# what makes it buildable on a machine with no X11, no ALSA and no GTK, which is
# what makes a second compiler and a second standard library cheap enough to run
# on every change.
#
# It is worth having for its own sake too: 159 tests that build in seconds are a
# different inner loop from 1046 that build in minutes.
#
# The same files stay in dew_tests above. Nothing should run on only one
# platform, and a file that runs in both places is the point.
# ==============================================================================
set(DEW_LANG_TEST_SOURCES
  ScoreCompletionTests.cpp
  ScoreCounterpointTests.cpp
  ScoreDeterminismTests.cpp
  ScoreDiagnosticTests.cpp
  ScoreGeneratorTests.cpp
  ScoreGridTests.cpp
  ScoreImitationTests.cpp
  ScoreLexerTests.cpp
  ScoreMusicTests.cpp
  ScoreParserTests.cpp
  ScoreResolverTests.cpp
)

add_executable(dew_lang_tests ${DEW_LANG_TEST_SOURCES})

target_link_libraries(dew_lang_tests
  PRIVATE
    dew_lang
    Catch2::Catch2WithMain
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

catch_discover_tests(dew_lang_tests TEST_PREFIX "lang: ")
