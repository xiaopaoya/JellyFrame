#pragma once

#include "render_core/arena.h"
#include "render_core/dom.h"
#include "render_core/feature_config.h"
#include "render_core/geometry.h"
#include "render_core/render_tree.h"
#include "render_core/style.h"
#include "render_core/text_backend.h"

#include <memory>
#include <string>
#include <vector>

namespace jellyframe {

struct LayoutBox;

struct LayoutBoxDeleter {
    bool arena_owned = false;
    void operator()(LayoutBox* box) const;
};

using LayoutBoxPtr = std::unique_ptr<LayoutBox, LayoutBoxDeleter>;

// This is intentionally owned by the layout box: a layout tree and its layer
// tree are one frame-local snapshot, so the result cannot outlive its inputs.
struct TextLayoutCache {
    bool valid = false;
    std::string source_text;
    std::string rendered_text;
    std::vector<std::string> lines;
    TextMeasureProvider text_measure;
    int available_width = 0;
    int content_width = 0;
    int rect_width = 0;
    int rect_height = 0;
    int viewport_width = 0;
    int viewport_height = 0;
    int font_size = 0;
    int font_weight = 0;
    std::uint32_t font_family_hash = 0;
    int line_height = 0;
    int text_indent = 0;
    int letter_spacing = 0;
    TextTransform text_transform = TextTransform::None;
    bool overflow_wrap_anywhere = false;
    bool white_space_nowrap = false;
    bool text_overflow_ellipsis = false;
    TextAlign text_align = TextAlign::Start;
};

struct LayoutBox {
    const Node* node = nullptr;
    Style style;
    Rect rect;
    int viewport_width = 0;
    int viewport_height = 0;
    TextLayoutCache text_layout_cache;
    std::vector<LayoutBoxPtr> children;
};

struct LayoutEngineOptions {
    std::size_t max_layout_boxes = 4096;
    DiagnosticSink* diagnostics = nullptr;
    std::size_t max_layout_depth = 64;
};

class LayoutEngine {
public:
    explicit LayoutEngine(const StyleResolver& style_resolver,
                          TextMeasureProvider text_measure = {},
                          LayoutEngineOptions options = {});

    LayoutBoxPtr layout(const Node& root, int viewport_width) const;
    LayoutBoxPtr layout(const Node& root, int viewport_width, int viewport_height) const;
    LayoutBoxPtr layout(const Node& root, int viewport_width, MonotonicArena& arena) const;
    LayoutBoxPtr layout(const Node& root, int viewport_width, int viewport_height, MonotonicArena& arena) const;
    LayoutBoxPtr layout(const RenderObject& render_tree, int viewport_width) const;
    LayoutBoxPtr layout(const RenderObject& render_tree, int viewport_width, int viewport_height) const;
    LayoutBoxPtr layout(const RenderObject& render_tree, int viewport_width, MonotonicArena& arena) const;
    LayoutBoxPtr layout(const RenderObject& render_tree, int viewport_width, int viewport_height, MonotonicArena& arena) const;

private:
    const StyleResolver& style_resolver_;
    TextMeasureProvider text_measure_;
    LayoutEngineOptions options_;

    int layout_box(LayoutBox& box, int x, int y, int width, int height, std::size_t depth) const;
    int layout_text_box(LayoutBox& box,
                        int border_box_x,
                        int border_box_y,
                        int content_width,
                        int min_width,
                        int height) const;
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    int layout_flex_box(LayoutBox& box,
                        int content_x,
                        int content_y,
                        int content_width,
                        int containing_height,
                        std::size_t depth) const;
    int layout_grid_box(LayoutBox& box,
                        int content_x,
                        int content_y,
                        int content_width,
                        int containing_height,
                        std::size_t depth) const;
#endif
    int layout_inline_children(LayoutBox& box, int content_x, int content_y, int content_width, std::size_t depth) const;
    void layout_positioned_children(LayoutBox& box,
                                    int content_x,
                                    int content_y,
                                    int content_width,
                                    int content_height,
                                    int viewport_width,
                                    std::size_t depth) const;
    LayoutBoxPtr build_with_arena(const RenderObject& render_tree,
                                  int viewport_width,
                                  int viewport_height,
                                  MonotonicArena* arena) const;
    void build_layout_tree(const RenderObject& object, LayoutBox& box, MonotonicArena* arena) const;
    LayoutBoxPtr make_layout_box(MonotonicArena* arena) const;
};

std::size_t count_layout_boxes(const LayoutBox& root);

} // namespace jellyframe
