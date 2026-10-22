# Slapper test suite (Catch2 v3): ctest --test-dir build -R sl_
juce_add_console_app(sl_tests PRODUCT_NAME "sl_tests")
target_sources(sl_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp
    SlTests.cpp)
target_include_directories(sl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(sl_tests PRIVATE
    SlapperCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(sl_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(sl_tests)

catch_discover_tests(sl_tests
    TEST_PREFIX "sl_"
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 900)

# Demos (WAV, one riff per preset + the playing engine):
#   build/tests/slapper/sl_render_artefacts/Release/sl_render renders/slapper
juce_add_console_app(sl_render PRODUCT_NAME "sl_render")
target_sources(sl_render PRIVATE SlRender.cpp)
target_include_directories(sl_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(sl_render PRIVATE SlapperCode Catch2::Catch2 juce::juce_recommended_config_flags)
target_compile_definitions(sl_render PRIVATE JUCE_STANDALONE_APPLICATION=1)

# Editor screenshot: build/tests/slapper/sl_screenshots_artefacts/Release/sl_screenshots <dir>
juce_add_console_app(sl_screenshots PRODUCT_NAME "sl_screenshots")
target_sources(sl_screenshots PRIVATE SlScreenshots.cpp)
target_include_directories(sl_screenshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(sl_screenshots PRIVATE SlapperCode juce::juce_recommended_config_flags)
target_compile_definitions(sl_screenshots PRIVATE JUCE_STANDALONE_APPLICATION=1)
