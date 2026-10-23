# Crackle test suite (Catch2 v3): ctest --test-dir build -R cr_
juce_add_console_app(cr_tests PRODUCT_NAME "cr_tests")
target_sources(cr_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp
    CrTests.cpp)
target_include_directories(cr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(cr_tests PRIVATE
    CrackleCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(cr_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(cr_tests)

catch_discover_tests(cr_tests
    TEST_PREFIX "cr_"
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 900)

# Editor screenshots: build/tests/crackle/cr_screenshots_artefacts/Release/cr_screenshots [dir]
juce_add_console_app(cr_screenshots PRODUCT_NAME "cr_screenshots")
target_sources(cr_screenshots PRIVATE CrScreenshots.cpp)
target_include_directories(cr_screenshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(cr_screenshots PRIVATE CrackleCode juce::juce_recommended_config_flags)
target_compile_definitions(cr_screenshots PRIVATE JUCE_STANDALONE_APPLICATION=1)
