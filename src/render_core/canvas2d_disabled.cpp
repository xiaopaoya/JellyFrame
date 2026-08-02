#include "render_core/canvas2d.h"

namespace jellyframe {

Canvas2DRegistry::Canvas2DRegistry(Canvas2DPolicy policy)
    : policy_(policy) {
    policy_.enabled = false;
    (void)next_handle_;
    (void)next_gradient_id_;
}

void Canvas2DRegistry::set_policy(Canvas2DPolicy policy) {
    policy_ = policy;
    policy_.enabled = false;
}

void Canvas2DRegistry::set_text_backend(TextMeasureProvider, TextPainter) {}

void Canvas2DRegistry::clear() {}

std::uint32_t Canvas2DRegistry::ensure_surface(Node&) { return 0; }

std::uint32_t Canvas2DRegistry::handle_for(const Node&) const { return 0; }

const Canvas2DSurface* Canvas2DRegistry::surface(std::uint32_t) const { return nullptr; }

bool Canvas2DRegistry::set_fill_style(Node&, std::string_view) { return false; }
bool Canvas2DRegistry::set_stroke_style(Node&, std::string_view) { return false; }
bool Canvas2DRegistry::set_fill_gradient(Node&, std::uint32_t) { return false; }
bool Canvas2DRegistry::set_stroke_gradient(Node&, std::uint32_t) { return false; }
bool Canvas2DRegistry::set_line_width(Node&, double) { return false; }
bool Canvas2DRegistry::set_global_alpha(Node&, double) { return false; }
bool Canvas2DRegistry::set_font(Node&, std::string_view) { return false; }
bool Canvas2DRegistry::translate(Node&, double, double) { return false; }
bool Canvas2DRegistry::reset_transform(Node&) { return false; }

Color Canvas2DRegistry::fill_style(const Node&) const { return Color{0, 0, 0, 255}; }
Color Canvas2DRegistry::stroke_style(const Node&) const { return Color{0, 0, 0, 255}; }
int Canvas2DRegistry::line_width(const Node&) const { return 1; }
double Canvas2DRegistry::global_alpha(const Node&) const { return 1.0; }
std::string Canvas2DRegistry::font(const Node&) const { return "10px sans-serif"; }

bool Canvas2DRegistry::save(Node&) { return false; }
bool Canvas2DRegistry::restore(Node&) { return false; }
bool Canvas2DRegistry::clear_rect(Node&, int, int, int, int) { return false; }
bool Canvas2DRegistry::fill_rect(Node&, int, int, int, int) { return false; }
bool Canvas2DRegistry::stroke_rect(Node&, int, int, int, int) { return false; }
bool Canvas2DRegistry::begin_path(Node&) { return false; }
bool Canvas2DRegistry::move_to(Node&, int, int) { return false; }
bool Canvas2DRegistry::line_to(Node&, int, int) { return false; }
bool Canvas2DRegistry::quadratic_curve_to(Node&, double, double, double, double) { return false; }
bool Canvas2DRegistry::bezier_curve_to(Node&, double, double, double, double, double, double) { return false; }
bool Canvas2DRegistry::arc(Node&, double, double, double, double, double, bool) { return false; }
bool Canvas2DRegistry::close_path(Node&) { return false; }
bool Canvas2DRegistry::fill(Node&) { return false; }
bool Canvas2DRegistry::stroke(Node&) { return false; }
Canvas2DTextMetrics Canvas2DRegistry::measure_text(Node&, std::string_view) { return {}; }
bool Canvas2DRegistry::fill_text(Node&, std::string_view, double, double, double) { return false; }

bool Canvas2DRegistry::draw_image(Node&, const Node&, int, int, int, int, int, int, int, int) {
    return false;
}

std::uint32_t Canvas2DRegistry::create_linear_gradient(double, double, double, double) { return 0; }
std::uint32_t Canvas2DRegistry::create_linear_gradient(Node&, double, double, double, double) { return 0; }
std::uint32_t Canvas2DRegistry::create_radial_gradient(double, double, double, double, double, double) { return 0; }
std::uint32_t Canvas2DRegistry::create_radial_gradient(Node&, double, double, double, double, double, double) { return 0; }
bool Canvas2DRegistry::add_color_stop(std::uint32_t, double, std::string_view) { return false; }

Canvas2DSurface* Canvas2DRegistry::mutable_surface(std::uint32_t) { return nullptr; }
Canvas2DSurface* Canvas2DRegistry::surface_for(Node&) { return nullptr; }
const Canvas2DSurface* Canvas2DRegistry::surface_for(const Node&) const { return nullptr; }
const Canvas2DGradient* Canvas2DRegistry::gradient(std::uint32_t) const { return nullptr; }
bool Canvas2DRegistry::gradient_exists(std::uint32_t) const { return false; }
std::size_t Canvas2DRegistry::total_pixels() const { return 0; }

bool is_canvas2d_handle(std::uint32_t) { return false; }

} // namespace jellyframe
