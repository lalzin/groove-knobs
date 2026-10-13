# Boogie Trio test suite (Catch2 v3): ctest --test-dir build -R bt_
juce_add_console_app(bt_tests PRODUCT_NAME "bt_tests")
target_sources(bt_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp
    BtDspTests.cpp
    BtStateTests.cpp
    BtMakeupTests.cpp)
target_include_directories(bt_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(bt_tests PRIVATE
    BoogieTrioCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(bt_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(bt_tests)

catch_discover_tests(bt_tests
    TEST_PREFIX "bt_"
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 900)

# Editor screenshots: build/tests/boogie_trio/bt_screenshots_artefacts/Release/bt_screenshots
juce_add_console_app(bt_screenshots PRODUCT_NAME "bt_screenshots")
target_sources(bt_screenshots PRIVATE BtScreenshots.cpp)
target_link_libraries(bt_screenshots PRIVATE BoogieTrioCode juce::juce_recommended_config_flags)
target_compile_definitions(bt_screenshots PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(bt_screenshots)
