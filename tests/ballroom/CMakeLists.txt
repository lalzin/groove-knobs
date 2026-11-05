# BALLROOM test suite (Catch2 v3): ctest --test-dir build -R bl_
juce_add_console_app(bl_tests PRODUCT_NAME "bl_tests")
target_sources(bl_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp
    BlTests.cpp)
target_include_directories(bl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(bl_tests PRIVATE
    BallroomCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(bl_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(bl_tests)

catch_discover_tests(bl_tests
    TEST_PREFIX "bl_"
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 900)

# Editor screenshots: build/tests/headspin/bl_screenshots_artefacts/Release/bl_screenshots [dir]
juce_add_console_app(bl_screenshots PRODUCT_NAME "bl_screenshots")
target_sources(bl_screenshots PRIVATE BlScreenshots.cpp)
target_include_directories(bl_screenshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(bl_screenshots PRIVATE BallroomCode juce::juce_recommended_config_flags)
target_compile_definitions(bl_screenshots PRIVATE JUCE_STANDALONE_APPLICATION=1)
