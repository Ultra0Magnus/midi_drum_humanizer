cmake_minimum_required(VERSION 3.22)

project(MidiDrumHumanizer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C++ runtime statically, so no Visual C++ redistributable is needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MDH_BUILD_PLUGIN "Build the plugin (downloads JUCE)" ON)
option(MDH_BUILD_TESTS "Build the engine unit tests" ON)
option(MDH_COPY_PLUGIN_AFTER_BUILD "Install the plugin into the system plugin folder after building" OFF)

# The humanizer itself: plain C++, no JUCE, so it can be unit tested on its own.
add_library(humanizer_engine STATIC src/engine/HumanizerEngine.cpp)
target_include_directories(humanizer_engine PUBLIC src/engine)
set_target_properties(humanizer_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MDH_BUILD_PLUGIN)
    include(FetchContent)

    # To use an existing JUCE checkout: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    set(MDH_FORMATS VST3)
    if(APPLE)
        list(APPEND MDH_FORMATS AU)
    endif()

    juce_add_plugin(MidiDrumHumanizer
        PRODUCT_NAME "MIDI Drum Humanizer"
        COMPANY_NAME "Ultra0Magnus"
        BUNDLE_ID "com.ultra0magnus.mididrumhumanizer"
        PLUGIN_MANUFACTURER_CODE Ulmg
        PLUGIN_CODE Dhum
        FORMATS ${MDH_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Tools
        AU_MAIN_TYPE kAudioUnitType_MusicEffect
        COPY_PLUGIN_AFTER_BUILD ${MDH_COPY_PLUGIN_AFTER_BUILD})

    target_sources(MidiDrumHumanizer PRIVATE
        src/plugin/HitMonitor.cpp
        src/plugin/LookAndFeel.cpp
        src/plugin/Parameters.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/PluginProcessor.cpp)

    target_include_directories(MidiDrumHumanizer PRIVATE src/plugin)

    target_compile_definitions(MidiDrumHumanizer PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(MidiDrumHumanizer
        PRIVATE
            humanizer_engine
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

if(MDH_BUILD_TESTS)
    enable_testing()
    add_executable(engine_tests tests/EngineTests.cpp)
    target_link_libraries(engine_tests PRIVATE humanizer_engine)
    add_test(NAME engine_tests COMMAND engine_tests)
endif()
