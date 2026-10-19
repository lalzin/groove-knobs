# Pew Pew test suite (Catch2 v3): ctest --test-dir build -R pp_
juce_add_console_app(pp_tests PRODUCT_NAME "pp_tests")
target_sources(pp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp
    PpTests.cpp)
target_include_directories(pp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(pp_tests PRIVATE
    PewPewCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(pp_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(pp_tests)

catch_discover_tests(pp_tests
    TEST_PREFIX "pp_"
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 900)

# Editor screenshots: build/tests/pew_pew/pp_screenshots_artefacts/Release/pp_screenshots
juce_add_console_app(pp_screenshots PRODUCT_NAME "pp_screenshots")
target_sources(pp_screenshots PRIVATE PpScreenshots.cpp)
target_include_directories(pp_screenshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(pp_screenshots PRIVATE PewPewCode juce::juce_recommended_config_flags)
target_compile_definitions(pp_screenshots PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(pp_screenshots)
