juce_add_console_app(cr_render PRODUCT_NAME "cr_render")
target_sources(cr_render PRIVATE Main.cpp)
target_include_directories(cr_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gk_render)
target_link_libraries(cr_render PRIVATE
    CrackleCode
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)
target_compile_definitions(cr_render PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(cr_render)
