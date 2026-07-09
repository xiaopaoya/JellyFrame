#pragma once

#include "render_core/dom.h"
#include "render_core/geometry.h"
#include "render_core/software_renderer.h"
#include "render_core/text_backend.h"

#include <cstddef>
#include <cstdint>
#include <string>
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
    std::size_t max_path_points = 256;
    std::size_t max_state_stack_depth = 8;
    std::size_t max_gradients = 16;
    std::size_t max_gradient_stops = 4;
};

struct Canvas2DPoint {
    int x = 0;
    int y = 0;
};

enum class Canvas2DPaintKind {
    Solid,
    LinearGradient,
    RadialGradient,
};

enum class Canvas2DGradientKind {
    Linear,
    Radial,
};

struct Canvas2DPaintStyle {
    Canvas2DPaintKind kind = Canvas2DPaintKind::Solid;
    Color color{0, 0, 0, 255};
    std::uint32_t gradient_id = 0;
};

struct Canvas2DGradientStop {
    double offset = 0.0;
    Color color{0, 0, 0, 255};
};

struct Canvas2DGradient {
    std::uint32_t id = 0;
    Canvas2DGradientKind kind = Canvas2DGradientKind::Linear;
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double r0 = 0.0;
    double r1 = 0.0;
    std::vector<Canvas2DGradientStop> stops;
};

struct Canvas2DState {
    Canvas2DPaintStyle fill_style;
    Canvas2DPaintStyle stroke_style;
    int line_width = 1;
    double global_alpha = 1.0;
    int font_size = 10;
    int font_weight = 400;
    std::uint32_t font_family_hash = 0;
    int translate_x = 0;
    int translate_y = 0;
    std::string font = "10px sans-serif";
};

struct Canvas2DTextMetrics {
    double width = 0.0;
};

struct Canvas2DSurface {
    std::uint32_t handle = 0;
    const Node* node = nullptr;
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;
    Canvas2DState state;
    std::vector<Canvas2DState> state_stack;
    std::vector<Canvas2DPoint> path;
    std::vector<int> fill_intersections;
    bool path_closed = false;
};

class Canvas2DRegistry {
public:
    explicit Canvas2DRegistry(Canvas2DPolicy policy = {});

    void set_policy(Canvas2DPolicy policy);
    const Canvas2DPolicy& policy() const { return policy_; }
    void set_text_backend(TextMeasureProvider measure, TextPainter painter);
    void clear();

    std::uint32_t ensure_surface(Node& node);
    std::uint32_t handle_for(const Node& node) const;
    const Canvas2DSurface* surface(std::uint32_t handle) const;

    bool set_fill_style(Node& node, std::string_view value);
    bool set_stroke_style(Node& node, std::string_view value);
    bool set_fill_gradient(Node& node, std::uint32_t gradient_id);
    bool set_stroke_gradient(Node& node, std::uint32_t gradient_id);
    bool set_line_width(Node& node, double value);
    bool set_global_alpha(Node& node, double value);
    bool set_font(Node& node, std::string_view value);
    bool translate(Node& node, double x, double y);
    Color fill_style(const Node& node) const;
    Color stroke_style(const Node& node) const;
    int line_width(const Node& node) const;
    double global_alpha(const Node& node) const;
    std::string font(const Node& node) const;
    bool save(Node& node);
    bool restore(Node& node);

    bool clear_rect(Node& node, int x, int y, int width, int height);
    bool fill_rect(Node& node, int x, int y, int width, int height);
    bool stroke_rect(Node& node, int x, int y, int width, int height);
    bool begin_path(Node& node);
    bool move_to(Node& node, int x, int y);
    bool line_to(Node& node, int x, int y);
    bool quadratic_curve_to(Node& node, double control_x, double control_y, double x, double y);
    bool bezier_curve_to(Node& node, double control1_x, double control1_y, double control2_x, double control2_y, double x, double y);
    bool arc(Node& node, double x, double y, double radius, double start_angle, double end_angle, bool anticlockwise);
    bool close_path(Node& node);
    bool fill(Node& node);
    bool stroke(Node& node);
    Canvas2DTextMetrics measure_text(Node& node, std::string_view text);
    bool fill_text(Node& node, std::string_view text, double x, double y, double max_width = 0.0);
    bool draw_image(Node& destination,
                    const Node& source,
                    int source_x,
                    int source_y,
                    int source_width,
                    int source_height,
                    int destination_x,
                    int destination_y,
                    int destination_width,
                    int destination_height);
    std::uint32_t create_linear_gradient(double x0, double y0, double x1, double y1);
    std::uint32_t create_linear_gradient(Node& node, double x0, double y0, double x1, double y1);
    std::uint32_t create_radial_gradient(double x0, double y0, double r0,
                                         double x1, double y1, double r1);
    std::uint32_t create_radial_gradient(Node& node, double x0, double y0, double r0,
                                         double x1, double y1, double r1);
    bool add_color_stop(std::uint32_t gradient_id, double offset, std::string_view color);

private:
    Canvas2DPolicy policy_;
    TextMeasureProvider text_measure_;
    TextPainter text_painter_;
    std::uint32_t next_handle_ = 1;
    std::uint32_t next_gradient_id_ = 1;
    std::vector<Canvas2DSurface> surfaces_;
    std::vector<Canvas2DGradient> gradients_;

    Canvas2DSurface* mutable_surface(std::uint32_t handle);
    Canvas2DSurface* surface_for(Node& node);
    const Canvas2DSurface* surface_for(const Node& node) const;
    const Canvas2DGradient* gradient(std::uint32_t gradient_id) const;
    bool gradient_exists(std::uint32_t gradient_id) const;
    std::size_t total_pixels() const;
};

bool is_canvas2d_handle(std::uint32_t handle);

} // namespace jellyframe
