# kl_render — listening renders of Kick Lock (bass loop + 124 BPM kick on the sidechain)
juce_add_console_app(kl_render PRODUCT_NAME "kl_render")
target_sources(kl_render PRIVATE Main.cpp)
target_include_directories(kl_render PRIVATE ${CMAKE_SOURCE_DIR}/tests/common)
target_link_libraries(kl_render PRIVATE
    KickLockCode
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)
target_compile_definitions(kl_render PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(kl_render)
