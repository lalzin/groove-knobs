# HEAD SPIN test suite (Catch2 v3): ctest --test-dir build -R hs_
juce_add_console_app(hs_tests PRODUCT_NAME "hs_tests")
target_sources(hs_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp
    HsTests.cpp)
target_include_directories(hs_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(hs_tests PRIVATE
    HeadSpinCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(hs_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(hs_tests)

catch_discover_tests(hs_tests
    TEST_PREFIX "hs_"
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 900)

# Editor screenshots: build/tests/headspin/hs_screenshots_artefacts/Release/hs_screenshots [dir]
juce_add_console_app(hs_screenshots PRODUCT_NAME "hs_screenshots")
target_sources(hs_screenshots PRIVATE HsScreenshots.cpp)
target_include_directories(hs_screenshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(hs_screenshots PRIVATE HeadSpinCode juce::juce_recommended_config_flags)
target_compile_definitions(hs_screenshots PRIVATE JUCE_STANDALONE_APPLICATION=1)
