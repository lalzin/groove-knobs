# asset_prep — turns the mascot sprite sheet (green screen) into keyed PNG frames.
#   asset_prep <out-dir> [--cols 4] [--rows 3] [--height 560] <sheet>:<i,j,...> ...
juce_add_console_app(asset_prep PRODUCT_NAME "asset_prep")
target_sources(asset_prep PRIVATE Main.cpp)
target_link_libraries(asset_prep PRIVATE juce::juce_graphics juce::juce_recommended_config_flags)
target_compile_definitions(asset_prep PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
gk_enable_warnings(asset_prep)
