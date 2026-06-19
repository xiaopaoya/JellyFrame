set(JELLYFRAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRCS
        "${JELLYFRAME_ROOT}/src/render_core/arena.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/bitmap_font.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/css_parser.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/display_invalidation.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/dirty_region.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/document_script.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/document_style.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/dom.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/dom_owner.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/embedded_framebuffer.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/event.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/form_control.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/frame_loop.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/frame_update.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/hit_test.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/html_parser.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/html_tokenizer.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/html_tree_builder.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/input.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/layer_tree.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/layout.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/pipeline_statistics.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/render_tree.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/software_renderer.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/style.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/text_adapter.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/text_backend.cpp"
        "${JELLYFRAME_ROOT}/src/render_core/text_normalization.cpp"
    INCLUDE_DIRS
        "${JELLYFRAME_ROOT}/src"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wno-missing-field-initializers
)
