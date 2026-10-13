# High warning level for our own targets. JUCE's recommended warning flags
# already enable -Wall -Wextra (/W4 on MSVC) plus a set of extra checks and
# JUCE itself compiles cleanly with them, so we build on top of that.
function(gk_enable_warnings target)
    target_link_libraries(${target} PRIVATE juce::juce_recommended_warning_flags)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
    if(GK_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        else()
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
