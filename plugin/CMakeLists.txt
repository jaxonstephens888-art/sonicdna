# SONIC DNA — JUCE plugin (VST3 + Standalone; AU on macOS)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/JUCE ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

set(SDNA_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND SDNA_FORMATS AU)
endif()

juce_add_plugin(SonicDNA
  COMPANY_NAME "Sonic DNA"
  COMPANY_WEBSITE "https://example.invalid/sonicdna"
  BUNDLE_ID "audio.sonicdna.plugin"
  PLUGIN_MANUFACTURER_CODE Sdna
  PLUGIN_CODE Sdn1
  FORMATS ${SDNA_FORMATS}
  PRODUCT_NAME "SONIC DNA"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Drum Sampler
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  MICROPHONE_PERMISSION_ENABLED FALSE)

juce_generate_juce_header(SonicDNA)

juce_add_binary_data(SonicDNAData SOURCES
  Resources/fonts/Inter-Regular.otf
  Resources/fonts/Inter-Medium.otf
  Resources/fonts/Inter-SemiBold.otf
  Resources/fonts/JetBrainsMono-Regular.ttf
  Resources/fonts/JetBrainsMono-Medium.ttf)

target_sources(SonicDNA PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/ui/Theme.cpp
  Source/ui/ProfilePanel.cpp
  Source/ui/LibraryView.cpp
  Source/ui/DrumMachineView.cpp
  Source/services/Services.cpp)

target_include_directories(SonicDNA PRIVATE Source)
if(MSVC)
  target_compile_options(SonicDNA PRIVATE /utf-8 /bigobj)
endif()

target_compile_definitions(SonicDNA PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=1
  JUCE_USE_MP3AUDIOFORMAT=1
  JUCE_USE_FLAC=1
  JUCE_USE_OGGVORBIS=1
  JUCE_USE_WINDOWS_MEDIA_FORMAT=1
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0
  DONT_SET_USING_JUCE_NAMESPACE=1)

target_link_libraries(SonicDNA
  PRIVATE
    SonicDNAData
    sdna_core
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# On Linux JUCE's networking (used only for optional online analysis) goes through libcurl.
if(UNIX AND NOT APPLE)
  find_package(CURL REQUIRED)
  target_link_libraries(SonicDNA PRIVATE CURL::libcurl)
endif()

# Host-side integration test: loads the built VST3 like a DAW.
option(SDNA_BUILD_HOST_TEST "Build the VST3 hosting integration test" ON)
if(SDNA_BUILD_HOST_TEST)
  juce_add_console_app(SonicDNAHostTest PRODUCT_NAME "sdna_host_test")
  target_sources(SonicDNAHostTest PRIVATE ${CMAKE_SOURCE_DIR}/tests/host/HostTest.cpp)
  target_compile_definitions(SonicDNAHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  juce_generate_juce_header(SonicDNAHostTest)
  target_link_libraries(SonicDNAHostTest PRIVATE juce::juce_audio_utils juce::juce_audio_processors PUBLIC juce::juce_recommended_config_flags)
endif()
