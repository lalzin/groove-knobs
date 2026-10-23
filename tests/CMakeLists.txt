# Test suite (Catch2 v3). Run with:  ctest --test-dir build --output-on-failure
juce_add_console_app(ff_tests PRODUCT_NAME "ff_tests")
target_sources(ff_tests PRIVATE
    TestMain.cpp
    DspTests.cpp
    StateTests.cpp
    MakeupTests.cpp)
target_include_directories(ff_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(ff_tests PRIVATE
    FrenchFilterCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(ff_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(ff_tests)

include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(ff_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 900)

# Editor screenshots (spec §9.3): build/tests/ff_screenshots_artefacts/Release/ff_screenshots screenshots
juce_add_console_app(ff_screenshots PRODUCT_NAME "ff_screenshots")
target_sources(ff_screenshots PRIVATE Screenshots.cpp)
target_include_directories(ff_screenshots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(ff_screenshots PRIVATE FrenchFilterCode juce::juce_recommended_config_flags)

# Boogie Trio
add_subdirectory(boogie_trio)
add_subdirectory(backwash)
add_subdirectory(pew_pew)
add_subdirectory(sing_it_back)
add_subdirectory(kick_lock)
add_subdirectory(slapper)
add_subdirectory(crackle)
target_compile_definitions(ff_screenshots PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(ff_screenshots)
