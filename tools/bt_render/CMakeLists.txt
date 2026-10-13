juce_add_console_app(bt_render PRODUCT_NAME "bt_render")
target_sources(bt_render PRIVATE Main.cpp)
target_include_directories(bt_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gk_render)
target_link_libraries(bt_render PRIVATE
    BoogieTrioCode
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)
target_compile_definitions(bt_render PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(bt_render)
