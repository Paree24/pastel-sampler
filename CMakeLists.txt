cmake_minimum_required(VERSION 3.22)
project(PastelSampler VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Reuse a local JUCE checkout when present so we don't re-download ~100MB.
set(LOCAL_JUCE_SRC "/home/paree/ai_experiments/ersa/build/_deps/juce-src")
if(EXISTS "${LOCAL_JUCE_SRC}/CMakeLists.txt")
  message(STATUS "Reusing local JUCE from ${LOCAL_JUCE_SRC}")
  FetchContent_Declare(JUCE SOURCE_DIR "${LOCAL_JUCE_SRC}")
else()
  FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
  )
endif()
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(PastelSamplerAssets
  HEADER_NAME PastelAssets.h
  NAMESPACE PastelAssets
  SOURCES
    Assets/Lato-Regular.ttf
    Assets/Lato-Bold.ttf
    Assets/Lato-Black.ttf
)

set(PASTEL_SOURCES
  Source/Params.cpp
  Source/Analysis.cpp
  Source/SynthEngine.cpp
  Source/DSPEngine.cpp
  Source/FileLibrary.cpp
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/PastelViews.cpp
  Source/PastelLookAndFeel.cpp
  Source/Widgets.cpp
  Source/PresetBrowser.cpp
  Source/PianoStrip.cpp
)

set(PASTEL_DEFS
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_USE_MP3AUDIOFORMAT=1
  JUCE_USE_OGGVORBIS=1
  JUCE_USE_FLAC=1
)

juce_add_plugin(PastelSampler
  COMPANY_NAME "Pastel"
  BUNDLE_ID "com.pastel.pastelsampler"
  PLUGIN_MANUFACTURER_CODE Past
  PLUGIN_CODE Psm1
  FORMATS VST3 Standalone
  PRODUCT_NAME "Pastel Sampler"
  DESCRIPTION "Chromatic single-sample instrument with sampler, additive and spectral engines"
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_SYNTH TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(PastelSampler)
target_sources(PastelSampler PRIVATE ${PASTEL_SOURCES})
target_include_directories(PastelSampler PRIVATE Source)
target_compile_definitions(PastelSampler PRIVATE ${PASTEL_DEFS})
target_link_libraries(PastelSampler PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  juce_audio_plugin_client
  PastelSamplerAssets
)

# Headless test harness (links the real plugin sources)
juce_add_console_app(PastelSamplerHarness PRODUCT_NAME "PastelSamplerHarness")
target_sources(PastelSamplerHarness PRIVATE ${PASTEL_SOURCES} Test/Harness.cpp)
target_include_directories(PastelSamplerHarness PRIVATE Source)
target_compile_definitions(PastelSamplerHarness PRIVATE ${PASTEL_DEFS} PASTEL_HARNESS=1)
target_link_libraries(PastelSamplerHarness PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  PastelSamplerAssets
)

# Host-load check: loads the built .vst3 through JUCE's VST3 hosting (dev only)
#   build/PastelSamplerHostCheck "build/PastelSampler_artefacts/Release/VST3/Pastel Sampler.vst3"
add_executable(PastelSamplerHostCheck Test/HostCheck.cpp)
target_link_libraries(PastelSamplerHostCheck PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_recommended_config_flags
)
target_compile_definitions(PastelSamplerHostCheck PRIVATE
  JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
add_dependencies(PastelSamplerHostCheck PastelSampler_VST3)
