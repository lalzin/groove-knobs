# DAFT CHUNK test suite (Catch2 v3): ctest --test-dir build -R dc_
juce_add_console_app(dc_tests PRODUCT_NAME "dc_tests")
target_sources(dc_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp
    DcAnalysisTests.cpp
    DcTests.cpp
    DcGeneratorTests.cpp)
target_include_directories(dc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(dc_tests PRIVATE
    DaftChunkCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(dc_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(dc_tests)

catch_discover_tests(dc_tests
    TEST_PREFIX "dc_"
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 900)

# Editor screenshots: build/tests/daft_chunk/dc_screenshots_artefacts/Release/dc_screenshots [dir]
juce_add_console_app(dc_screenshots PRODUCT_NAME "dc_screenshots")
target_sources(dc_screenshots PRIVATE DcScreenshots.cpp)
target_include_directories(dc_screenshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(dc_screenshots PRIVATE DaftChunkCode juce::juce_recommended_config_flags)
target_compile_definitions(dc_screenshots PRIVATE JUCE_STANDALONE_APPLICATION=1)
