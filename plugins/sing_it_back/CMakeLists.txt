# ---------------------------------------------------------------------------
# Sing It Back — a crowd singing along with the voice (fifth Groove Knobs plugin)
#   SingItBackCode  INTERFACE target (plugin, sib_render, sib_tests)
#   SingItBack      the VST3 / AU / Standalone plugin
# ---------------------------------------------------------------------------

juce_add_binary_data(SingItBackPresets
    HEADER_NAME SingItBackPresets.h
    NAMESPACE   SIBPresets
    SOURCES     Presets/default.json)
set_target_properties(SingItBackPresets PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(SingItBackCode INTERFACE)
target_sources(SingItBackCode INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/SingItBackDsp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/SingItBackProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/SingItBackEditor.cpp)
target_include_directories(SingItBackCode INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(SingItBackCode INTERFACE
    groove::groove_core
    GrooveCoreAssets
    SingItBackPresets
    juce::juce_audio_utils
    juce::juce_dsp)
target_compile_definitions(SingItBackCode INTERFACE
    SIB_PRODUCT_NAME="${SIB_PRODUCT_NAME}"
    GK_SUITE_NAME="${GK_SUITE_NAME}"
    GK_COMPANY_NAME="${GK_COMPANY_NAME}"
    SIB_VERSION_STRING="${SIB_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

if(GK_COPY_PLUGINS)
    set(_sib_copy TRUE)
else()
    set(_sib_copy FALSE)
endif()

juce_add_plugin(SingItBack
    PRODUCT_NAME                "${SIB_PRODUCT_NAME}"
    COMPANY_NAME                "${GK_COMPANY_NAME}"
    BUNDLE_ID                   "${SIB_BUNDLE_ID}"
    VERSION                     "${SIB_VERSION}"
    PLUGIN_MANUFACTURER_CODE    ${GK_MANUFACTURER_CODE}
    PLUGIN_CODE                 ${SIB_PLUGIN_CODE}
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "The standalone app processes your audio input."
    HARDENED_RUNTIME_ENABLED    TRUE
    HARDENED_RUNTIME_OPTIONS    com.apple.security.device.audio-input
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Pitch Shift
    DESCRIPTION                 "One-knob crowd singing along (${GK_SUITE_NAME})"
    COPY_PLUGIN_AFTER_BUILD     ${_sib_copy})

target_sources(SingItBack PRIVATE Source/PluginEntry.cpp)
target_link_libraries(SingItBack PRIVATE
    SingItBackCode
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
gk_enable_warnings(SingItBack)
