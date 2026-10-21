# ---------------------------------------------------------------------------
# Kick Lock — sidechain ducking with sub phase lock (sixth Groove Knobs plugin)
#   KickLockCode  INTERFACE target (plugin, kl_render, kl_tests)
#   KickLock      the VST3 / AU / Standalone plugin
# ---------------------------------------------------------------------------

juce_add_binary_data(KickLockPresets
    HEADER_NAME KickLockPresets.h
    NAMESPACE   KLPresets
    SOURCES     Presets/default.json)
set_target_properties(KickLockPresets PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Phaze's poses (scripts/make_phaze_frames.sh)
file(GLOB KL_PHAZE_FRAMES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Assets/phaze_*.png)
juce_add_binary_data(KickLockArt
    HEADER_NAME KickLockArt.h
    NAMESPACE   KLArt
    SOURCES     ${KL_PHAZE_FRAMES})
set_target_properties(KickLockArt PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(KickLockCode INTERFACE)
target_sources(KickLockCode INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/KickLockDsp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/KickLockProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/KickLockEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PhazeMascot.cpp)
target_include_directories(KickLockCode INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(KickLockCode INTERFACE
    groove::groove_core
    GrooveCoreAssets
    KickLockPresets
    KickLockArt
    juce::juce_audio_utils
    juce::juce_dsp)
target_compile_definitions(KickLockCode INTERFACE
    KL_PRODUCT_NAME="${KL_PRODUCT_NAME}"
    GK_SUITE_NAME="${GK_SUITE_NAME}"
    GK_COMPANY_NAME="${GK_COMPANY_NAME}"
    KL_VERSION_STRING="${KL_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

if(GK_COPY_PLUGINS)
    set(_kl_copy TRUE)
else()
    set(_kl_copy FALSE)
endif()

juce_add_plugin(KickLock
    PRODUCT_NAME                "${KL_PRODUCT_NAME}"
    COMPANY_NAME                "${GK_COMPANY_NAME}"
    BUNDLE_ID                   "${KL_BUNDLE_ID}"
    VERSION                     "${KL_VERSION}"
    PLUGIN_MANUFACTURER_CODE    ${GK_MANUFACTURER_CODE}
    PLUGIN_CODE                 ${KL_PLUGIN_CODE}
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
    VST3_CATEGORIES             Fx Dynamics
    DESCRIPTION                 "One-knob sidechain with sub phase lock (${GK_SUITE_NAME})"
    COPY_PLUGIN_AFTER_BUILD     ${_kl_copy})

target_sources(KickLock PRIVATE Source/PluginEntry.cpp)
target_link_libraries(KickLock PRIVATE
    KickLockCode
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
gk_enable_warnings(KickLock)
