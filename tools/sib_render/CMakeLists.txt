# sib_render — listening renders of Sing It Back (synthetic vocal phrase)
juce_add_console_app(sib_render PRODUCT_NAME "sib_render")
target_sources(sib_render PRIVATE Main.cpp)
target_include_directories(sib_render PRIVATE ${CMAKE_SOURCE_DIR}/tests/common)
target_link_libraries(sib_render PRIVATE
    SingItBackCode
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)
target_compile_definitions(sib_render PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(sib_render)
