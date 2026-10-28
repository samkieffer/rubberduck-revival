# JUCE plugin: AU (macOS), VST3, Standalone.
#
# JUCE is fetched from GitHub unless RD_JUCE_DIR points to a local checkout.
include(FetchContent)

set(RD_JUCE_DIR "" CACHE PATH "Local JUCE checkout (optional, otherwise fetched)")
if(RD_JUCE_DIR)
    add_subdirectory("${RD_JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

if(APPLE)
    set(RD_FORMATS AU VST3 Standalone)
    option(RD_INSTALL_PLUGINS "Copy AU/VST3 into ~/Library/Audio/Plug-Ins after building" ON)
else()
    set(RD_FORMATS VST3 Standalone)
    option(RD_INSTALL_PLUGINS "Copy the VST3 into the user plug-in folder after building" OFF)
endif()

juce_add_plugin(RubberDuckRevival
    PRODUCT_NAME "Rubber Duck Revival"
    COMPANY_NAME "Rubber Duck Revival"
    BUNDLE_ID com.samkieffer.rubberduckrevival
    PLUGIN_MANUFACTURER_CODE Rdrv
    PLUGIN_CODE Rdh3
    FORMATS ${RD_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${RD_INSTALL_PLUGINS}
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    DESCRIPTION "Re-creation of d-lusion's Rubber Duck H3O+ bassline machine")

target_sources(RubberDuckRevival PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StepEditor.cpp
    Source/KeyLayout.cpp
    Source/MidiExport.cpp
    Source/Theme.cpp
    Source/RdLookAndFeel.cpp
    Source/Widgets.cpp)

# Interface fonts (SIL Open Font License, see Fonts/OFL-*.txt).
juce_add_binary_data(RubberDuckFonts
    HEADER_NAME RdFonts.h
    NAMESPACE RdFonts
    SOURCES
        Fonts/BarlowCondensed-Medium.ttf
        Fonts/BarlowCondensed-SemiBold.ttf
        Fonts/BarlowCondensed-Bold.ttf
        Fonts/ShareTechMono-Regular.ttf
        Fonts/IBMPlexMono-Medium.ttf
        Fonts/IBMPlexMono-Bold.ttf)

target_compile_definitions(RubberDuckRevival PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(RubberDuckRevival
    PRIVATE
        rdcore
        RubberDuckFonts
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if(APPLE)
    # Keyboard layout lookup for the tracker-style note entry (KeyLayout.cpp).
    target_link_libraries(RubberDuckRevival PRIVATE "-framework Carbon")
endif()

# Headless processor tests (simulated host transport).
juce_add_console_app(RubberDuckPluginTests PRODUCT_NAME "RubberDuckPluginTests")
target_sources(RubberDuckPluginTests PRIVATE ${CMAKE_SOURCE_DIR}/tests/PluginTests.cpp)
# Modal loops: the editor tests pump the message loop so the editor's timers run.
target_compile_definitions(RubberDuckPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(RubberDuckPluginTests PRIVATE RubberDuckRevival rdcore juce::juce_audio_processors
    juce::juce_recommended_config_flags)
if(RD_BUILD_TOOLS AND EXISTS "${RD_CONTENT_DIR}/Acid_was_my_first_love.h30")
    add_test(NAME plugin_processor COMMAND RubberDuckPluginTests "${RD_CONTENT_DIR}")
endif()
