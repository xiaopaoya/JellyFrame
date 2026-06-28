#pragma once

#include "render_core/dom.h"
#include "render_core/geometry.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace jellyframe {

struct Canvas2DPolicy {
    bool enabled = true;
    std::size_t max_surfaces = 4;
    std::size_t max_surface_pixels = 300 * 300;
    std::size_t max_total_pixels = 300 * 300;
    int default_width = 300;
    int default_height = 150;
};

struct Canvas2DPoint {
    int x = 0;
    int y = 0;
};

struct Canvas2DSurface {
    std::uint32_t handle = 0;
    const Node* node = nullptr;
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;
    Color fill_style{0, 0, 0, 255};
    Color stroke_style{0, 0, 0, 255};
    int line_width = 1;
    std::vector<Canvas2DPoint> path;
};

class Canvas2DRegistry {
public:
    explicit Canvas2DRegistry(Canvas2DPolicy policy = {});

    void set_policy(Canvas2DPolicy policy);
    const Canvas2DPolicy& policy() const { return policy_; }
    void clear();

    std::uint32_t ensure_surface(Node& node);
    std::uint32_t handle_for(const Node& node) const;
    const Canvas2DSurface* surface(std::uint32_t handle) const;

    bool set_fill_style(Node& node, std::string_view value);
    bool set_stroke_style(Node& node, std::string_view value);
    bool set_line_width(Node& node, double value);
    Color fill_style(const Node& node) const;
    Color stroke_style(const Node& node) const;
    int line_width(const Node& node) const;

    bool clear_rect(Node& node, int x, int y, int width, int height);
    bool fill_rect(Node& node, int x, int y, int width, int height);
    bool stroke_rect(Node& node, int x, int y, int width, int height);
    bool begin_path(Node& node);
    bool move_to(Node& node, int x, int y);
    bool line_to(Node& node, int x, int y);
    bool stroke(Node& node);

private:
    Canvas2DPolicy policy_;
    std::uint32_t next_handle_ = 1;
    std::vector<Canvas2DSurface> surfaces_;

    Canvas2DSurface* mutable_surface(std::uint32_t handle);
    Canvas2DSurface* surface_for(Node& node);
    const Canvas2DSurface* surface_for(const Node& node) const;
    std::size_t total_pixels() const;
};

bool is_canvas2d_handle(std::uint32_t handle);

} // namespace jellyframe
