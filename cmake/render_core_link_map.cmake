# Add a linker map to a desktop validation executable.
#
# Render Core itself is a static library, so a final executable map is the
# useful portable evidence: it shows which feature-family objects survived
# archive extraction and dead stripping. The path is placed beside the target
# to work with the single-config desktop validation builds used by CI.
function(jellyframe_enable_render_core_link_map target)
    if(MSVC)
        target_link_options(${target} PRIVATE
            "/MAP:${target}.map")
    elseif(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(${target} PRIVATE
            "SHELL:-Xlinker /MAP:${target}.map")
    else()
        target_link_options(${target} PRIVATE
            "-Wl,-Map,${target}.map")
    endif()
endfunction()
