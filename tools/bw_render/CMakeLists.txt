juce_add_console_app(bw_render PRODUCT_NAME "bw_render")
target_sources(bw_render PRIVATE Main.cpp)
target_include_directories(bw_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gk_render)
target_link_libraries(bw_render PRIVATE
    BackwashCode
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)
target_compile_definitions(bw_render PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(bw_render)
