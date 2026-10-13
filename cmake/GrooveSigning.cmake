# macOS signing for local use.
#
# JUCE (>= 7) already ad-hoc signs every plugin bundle after the build and
# before COPY_PLUGIN_AFTER_BUILD copies it to ~/Library/Audio/Plug-Ins, which is
# what Apple Silicon requires to load a binary. This helper exists for the
# extra targets (tools, standalone) and so the scripts have one place to call.
#
#   codesign --force --deep --sign - <bundle>
function(gk_adhoc_sign target)
    if(APPLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND codesign --force --sign - "$<TARGET_FILE:${target}>"
            COMMENT "Ad-hoc signing ${target}"
            VERBATIM)
    endif()
endfunction()
