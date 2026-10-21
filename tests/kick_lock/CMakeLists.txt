# Kick Lock test suite (Catch2 v3): ctest --test-dir build -R kl_
juce_add_console_app(kl_tests PRODUCT_NAME "kl_tests")
target_sources(kl_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TestMain.cpp
    KlTests.cpp)
target_include_directories(kl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_link_libraries(kl_tests PRIVATE
    KickLockCode
    Catch2::Catch2
    juce::juce_recommended_config_flags)
target_compile_definitions(kl_tests PRIVATE JUCE_STANDALONE_APPLICATION=1)
gk_enable_warnings(kl_tests)

catch_discover_tests(kl_tests
    TEST_PREFIX "kl_"
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 1800)

