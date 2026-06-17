set(JELLYFRAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRCS
        "${JELLYFRAME_ROOT}/src/core/arena.cpp"
        "${JELLYFRAME_ROOT}/src/core/bitmap_font.cpp"
        "${JELLYFRAME_ROOT}/src/core/css_parser.cpp"
        "${JELLYFRAME_ROOT}/src/core/dirty_region.cpp"
        "${JELLYFRAME_ROOT}/src/core/document_script.cpp"
        "${JELLYFRAME_ROOT}/src/core/document_style.cpp"
        "${JELLYFRAME_ROOT}/src/core/dom.cpp"
        "${JELLYFRAME_ROOT}/src/core/embedded_framebuffer.cpp"
        "${JELLYFRAME_ROOT}/src/core/event.cpp"
        "${JELLYFRAME_ROOT}/src/core/form_control.cpp"
        "${JELLYFRAME_ROOT}/src/core/frame_update.cpp"
        "${JELLYFRAME_ROOT}/src/core/hit_test.cpp"
        "${JELLYFRAME_ROOT}/src/core/html_parser.cpp"
        "${JELLYFRAME_ROOT}/src/core/html_tokenizer.cpp"
        "${JELLYFRAME_ROOT}/src/core/html_tree_builder.cpp"
        "${JELLYFRAME_ROOT}/src/core/input.cpp"
        "${JELLYFRAME_ROOT}/src/core/layer_tree.cpp"
        "${JELLYFRAME_ROOT}/src/core/layout.cpp"
        "${JELLYFRAME_ROOT}/src/core/render_tree.cpp"
        "${JELLYFRAME_ROOT}/src/core/software_renderer.cpp"
        "${JELLYFRAME_ROOT}/src/core/style.cpp"
        "${JELLYFRAME_ROOT}/src/core/text_backend.cpp"
        "${JELLYFRAME_ROOT}/src/core/text_normalization.cpp"
    INCLUDE_DIRS
        "${JELLYFRAME_ROOT}/src"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wno-missing-field-initializers
)
