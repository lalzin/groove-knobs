# Sing It Back test suite (Catch2 v3): ctest --test-dir build -R sib_
juce_add_console_app(sib_tests PRODUCT_NAME "sib_tests")
target_sources(sib_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp
    SibTests.cpp)
target_include_directories(sib_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(sib_tests PRIVATE
    SingItBackCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(sib_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(sib_tests)

catch_discover_tests(sib_tests
    TEST_PREFIX "sib_"
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 1800)

# Editor screenshots: build/tests/sing_it_back/sib_screenshots_artefacts/Release/sib_screenshots
juce_add_console_app(sib_screenshots PRODUCT_NAME "sib_screenshots")
target_sources(sib_screenshots PRIVATE SibScreenshots.cpp)
target_include_directories(sib_screenshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(sib_screenshots PRIVATE SingItBackCode juce::juce_recommended_config_flags)
target_compile_definitions(sib_screenshots PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(sib_screenshots)
