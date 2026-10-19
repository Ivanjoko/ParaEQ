# Opciones de macOS: deben ir ANTES de project()
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Binario universal: Apple Silicon + Intel")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS minimo")

cmake_minimum_required(VERSION 3.22)
project(ParaEQ VERSION 2.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
  set(PLUGIN_FORMATS VST3 AU)      # FL Studio para Mac abre VST3 y AU
else()
  set(PLUGIN_FORMATS VST3)
endif()

juce_add_plugin(ParaEQ
  COMPANY_NAME "TuNombre"
  BUNDLE_ID com.tunombre.paraeq
  PLUGIN_MANUFACTURER_CODE Tnom
  PLUGIN_CODE Pqeq
  FORMATS ${PLUGIN_FORMATS}
  PRODUCT_NAME "ParaEQ"
  VST3_CATEGORIES Fx EQ
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD TRUE
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE)

target_sources(ParaEQ PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_compile_features(ParaEQ PRIVATE cxx_std_17)
target_compile_definitions(ParaEQ PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ParaEQ PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
