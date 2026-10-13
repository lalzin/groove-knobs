# Backwash test suite (Catch2 v3): ctest --test-dir build -R bw_
juce_add_console_app(bw_tests PRODUCT_NAME "bw_tests")
target_sources(bw_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp
    BwTests.cpp)
target_include_directories(bw_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(bw_tests PRIVATE
    BackwashCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(bw_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(bw_tests)

catch_discover_tests(bw_tests
    TEST_PREFIX "bw_"
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 900)

# Editor screenshots: build/tests/backwash/bw_screenshots_artefacts/Release/bw_screenshots
juce_add_console_app(bw_screenshots PRODUCT_NAME "bw_screenshots")
target_sources(bw_screenshots PRIVATE BwScreenshots.cpp)
target_link_libraries(bw_screenshots PRIVATE BackwashCode juce::juce_recommended_config_flags)
target_compile_definitions(bw_screenshots PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(bw_screenshots)
