# gb_render — offline MIDI renderer for the Groove Knobs instruments (Pew Pew)
juce_add_console_app(gb_render PRODUCT_NAME "gb_render")
target_sources(gb_render PRIVATE Main.cpp)
target_link_libraries(gb_render PRIVATE
    PewPewCode
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)
target_compile_definitions(gb_render PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(gb_render)
