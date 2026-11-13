cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
project(Zazza VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
add_library(zazza_engine Source/MusicEngine.cpp)
target_include_directories(zazza_engine PUBLIC Source)
if(BUILD_TESTING)
    add_executable(engine_tests Tests/EngineTests.cpp)
    target_link_libraries(engine_tests PRIVATE zazza_engine)
    add_test(NAME musical_engine COMMAND engine_tests)
endif()
option(ZAZZA_BUILD_PLUGIN "Build the VST3 and standalone instrument" ON)
if(ZAZZA_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG dabd5ad13cd869cba41046947fffc8e5ce5cd662)
    FetchContent_MakeAvailable(JUCE)
    juce_add_binary_data(ZazzaAssets SOURCES assets/wallpaper.gif assets/fonts/LuckiestGuy-Regular.ttf)
    juce_add_plugin(Zazza
        COMPANY_NAME "Zazza" BUNDLE_ID "audio.zazza.synth"
        PLUGIN_MANUFACTURER_CODE Zazz PLUGIN_CODE Zaz1
        PRODUCT_NAME "Zazza" FORMATS VST3 Standalone
        IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Synth)
    target_sources(Zazza PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/SampleAsset.cpp)
    target_compile_definitions(Zazza PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(Zazza PRIVATE zazza_engine ZazzaAssets juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(BUILD_TESTING)
        add_executable(processor_tests Tests/ProcessorTests.cpp)
        target_include_directories(processor_tests PRIVATE Source $<TARGET_PROPERTY:Zazza,INCLUDE_DIRECTORIES>)
        target_compile_definitions(processor_tests PRIVATE $<TARGET_PROPERTY:Zazza,COMPILE_DEFINITIONS>)
        target_link_libraries(processor_tests PRIVATE Zazza)
        add_test(NAME processor_integration COMMAND processor_tests)
    endif()
endif()
