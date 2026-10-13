juce_add_console_app(ff_render PRODUCT_NAME "ff_render")
target_sources(ff_render PRIVATE Main.cpp)
target_link_libraries(ff_render PRIVATE
    FrenchFilterCode
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)
target_compile_definitions(ff_render PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(ff_render)
