# ---------------------------------------------------------------------------
# Kick Lock Send — on the kick's track: sends its kicks to Kick Lock (no
# sidechain routing in the host). Ships with Kick Lock.
#   KickLockSendCode  INTERFACE target (plugin, tests)
#   KickLockSend      the VST3 / AU plugin
# ---------------------------------------------------------------------------

juce_add_binary_data(KickLockSendPresets
    HEADER_NAME KickLockSendPresets.h
    NAMESPACE   KLSPresets
    SOURCES     Presets/default.json)
set_target_properties(KickLockSendPresets PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(KickLockSendCode INTERFACE)
target_sources(KickLockSendCode INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/KickSendDsp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/KickSendProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/KickSendEditor.cpp)
target_include_directories(KickLockSendCode INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/../kick_lock/Source)
target_link_libraries(KickLockSendCode INTERFACE
    groove::groove_core
    GrooveCoreAssets
    KickLockSendPresets
    KickLockArt
    juce::juce_audio_utils
    juce::juce_dsp)
target_compile_definitions(KickLockSendCode INTERFACE
    KLS_PRODUCT_NAME="${KLS_PRODUCT_NAME}"
    GK_SUITE_NAME="${GK_SUITE_NAME}"
    GK_COMPANY_NAME="${GK_COMPANY_NAME}"
    KLS_VERSION_STRING="${KLS_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

if(GK_COPY_PLUGINS)
    set(_kls_copy TRUE)
else()
    set(_kls_copy FALSE)
endif()

juce_add_plugin(KickLockSend
    PRODUCT_NAME                "${KLS_PRODUCT_NAME}"
    COMPANY_NAME                "${GK_COMPANY_NAME}"
    BUNDLE_ID                   "${KLS_BUNDLE_ID}"
    VERSION                     "${KLS_VERSION}"
    PLUGIN_MANUFACTURER_CODE    ${GK_MANUFACTURER_CODE}
    PLUGIN_CODE                 ${KLS_PLUGIN_CODE}
    FORMATS                     VST3 AU
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    HARDENED_RUNTIME_ENABLED    TRUE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Tools
    DESCRIPTION                 "Sends the kicks of its track to Kick Lock (${GK_SUITE_NAME})"
    COPY_PLUGIN_AFTER_BUILD     ${_kls_copy})

target_sources(KickLockSend PRIVATE Source/PluginEntry.cpp)
target_link_libraries(KickLockSend PRIVATE
    KickLockSendCode
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
gk_enable_warnings(KickLockSend)
