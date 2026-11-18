#include "render_core/budget.h"
#include "render_core/css_parser.h"
#include "render_core/diagnostics.h"
#include "render_core/document_style.h"
#include "render_core/dom.h"
#include "render_core/frame_update.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/pipeline_statistics.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"
#include "render_core/style.h"
#include "display_command_access.h"
#include "native_budgets.h"
#include "native_paint.h"
#include "app_package.h"

#if defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME)
#include "native_contextual.h"
#endif

#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED) && defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME)
#include "native_image.h"
#include "app_runtime/app_frame_policy.h"
#include "render_core/fractional_image_sampling.h"
#endif

#if defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME) && \
    JELLYFRAME_NATIVE_CORE_TEXT_DEFAULT
#include "native_package_fonts.h"
#endif

#include "example_css_io.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

constexpr std::size_t kMaxInputBytes = 512 * 1024;

struct BrowserOptions {
    std::string html_path;
    std::string css_path;
    std::string output_path;
    std::string resource_root;
    std::string diagnostics_json_path;
    int viewport_width = 360;
    int viewport_height = 240;
    bool viewport_width_set = false;
    bool viewport_height_set = false;
    bool package_mode = false;
};

struct ImageFrameSinkContext {
    std::string path;
    bool ok = false;
};

#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED) && defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME)
// One synchronous owner spans metadata selection, layer resolution and paint.
// All jobs finish before any borrowed metadata is exposed to Core.
struct PseudoImages {
    AppRuntimeHost runtime{AppRuntimeHostOptions{64, 32, 64, 1024 * 1024, 4}};
    ImageDecodeMock decoder{ImageDecodePolicy{true, 1024, 256, 256, 256 * 256 * 4, 4}};
    AppImageSurfaceCache cache{AppImageSurfaceCacheOptions{8, 512 * 1024}};
    jellyframe_example::PackageResourceContext resources;
    const StyleResolver& resolver;
    std::vector<std::string> sources;
    DiagnosticSink* diagnostics;

    PseudoImages(const BrowserOptions& options, const StyleResolver& styles, DiagnosticSink* sink)
        : resolver(styles), diagnostics(sink) {
        runtime.launch("org.jellyframe.pseudo.images", AppRole::App);
        const auto html = std::filesystem::absolute(options.html_path);
        resources.root = options.resource_root.empty() ? html.parent_path() :
            std::filesystem::absolute(options.resource_root);
        resources.base_url = "/" + std::filesystem::relative(html, resources.root).generic_string();
        resources.max_input_bytes = kMaxInputBytes;
        resources.diagnostics = sink;
        const auto manifest_path = resources.root / "jellyframe.app.json";
        if (!options.package_mode && std::filesystem::exists(manifest_path)) {
            const auto manifest = jellyframe_example::parse_app_manifest_text(
                jellyframe_example::read_text_file_limited(manifest_path, kMaxInputBytes), false, true);
            if (!manifest.contextual_text_pipeline.empty())
                throw std::runtime_error("pseudo browser requires the scalar text pipeline");
        }
    }

    void prepare_source(const std::string& source) {
        if (source.empty() || std::find(sources.begin(), sources.end(), source) != sources.end()) return;
        if (sources.size() >= 8) throw std::runtime_error("pseudo image source budget exceeded");
        sources.push_back(source);
        std::string bytes;
        ImageDecodeFixture fixture;
        if (!jellyframe_example::load_package_resource(source, resources.base_url, bytes, &resources) ||
            !native::decode_bmp_to_fixture(source, bytes, fixture, diagnostics) ||
            !decoder.add_fixture(std::move(fixture)))
            throw std::runtime_error("pseudo image backend unavailable: " + source);
        std::uint32_t handle = 0;
        cache.resolve_or_request(runtime, decoder, source, &handle);
        AppFrameScratch scratch;
        scratch.reserve_from_options(AppRuntimeHostOptions{64, 32, 64, 1024 * 1024, 4});
        for (std::size_t pass = 0; pass < 8 && decoder.complete_next(runtime); ++pass) {
            runtime.pump_frame_completions(scratch);
            for (const auto& completion : scratch.accepted_completions) cache.handle_completion(completion);
        }
        if (!cache.ready_handle(runtime, source, &handle) || !decoder.surface(handle) ||
            cache.ready_byte_count() > 512 * 1024)
            throw std::runtime_error("pseudo image decode refused: " + source);
    }

    void prepare(const RenderObject& object) {
        if (object.node && object.node->type == NodeType::Element && object.node->tag_name == "img")
            prepare_source(object.node->attribute("src"));
        const auto resource_id = background_image_resource_id(object.style.background_overlay_packed);
        if (resource_id) {
            const auto* source = resolver.background_image_resource_url(resource_id);
            if (source) prepare_source(*source);
        }
        for (const auto& child : object.children) prepare(*child);
    }

    static bool resolve(const Node& node, ImageResolveKind kind, std::uint16_t id,
                        std::uint32_t& handle, void* raw) {
        auto& self = *static_cast<PseudoImages*>(raw);
        const auto* source = kind == ImageResolveKind::Background ?
            self.resolver.background_image_resource_url(id) : &node.attribute("src");
        return source && self.cache.ready_handle(self.runtime, *source, &handle);
    }

    static bool dimensions(std::uint32_t handle, int& width, int& height, void* raw) {
        const auto* surface = static_cast<PseudoImages*>(raw)->decoder.surface(handle);
        if (!surface || surface->width <= 0 || surface->height <= 0) return false;
        width = surface->width;
        height = surface->height;
        return true;
    }

    static bool natural_size(const Node& node, int& width, int& height, void* raw) {
        std::uint32_t handle = 0;
        return resolve(node, ImageResolveKind::Content, 0, handle, raw) &&
            dimensions(handle, width, height, raw);
    }

    static bool paint(FrameBuffer& target, const ImagePaintFractionalRequest& request, void* raw) {
        const auto* surface = static_cast<PseudoImages*>(raw)->decoder.surface(request.image_handle);
        if (!surface || surface->pixel_format != HostPixelFormat::Rgba8888 ||
            surface->width <= 0 || surface->height <= 0 || surface->stride_pixels < surface->width ||
            static_cast<std::size_t>(surface->stride_pixels) >
                surface->pixels.size() / 4 / static_cast<std::size_t>(surface->height)) return false;
        FractionalImageSamplingPlan plan;
        if (!prepare_fractional_image_sampling(request, surface->width, surface->height, target, plan)) return false;
        const auto read = [&](int x, int y) {
            const auto index = (static_cast<std::size_t>(y) * surface->stride_pixels + x) * 4;
            return Color{surface->pixels[index], surface->pixels[index + 1],
                         surface->pixels[index + 2], surface->pixels[index + 3]};
        };
        for (int y = plan.clip.y; y < plan.clip.y + plan.clip.height; ++y) {
            for (int x = plan.clip.x; x < plan.clip.x + plan.clip.width; ++x) {
                const Color source = sample_fractional_image(plan, x, y, read);
                Color& destination = target.pixel(x, y);
                const int inverse = 255 - source.a;
                const int alpha = source.a + (destination.a * inverse + 127) / 255;
                if (!alpha) { destination = {0, 0, 0, 0}; continue; }
                const auto blend = [&](int src, int dst) {
                    return static_cast<std::uint8_t>((src * source.a +
                        (dst * destination.a * inverse + 127) / 255 + alpha / 2) / alpha);
                };
                destination = {blend(source.r, destination.r), blend(source.g, destination.g),
                               blend(source.b, destination.b), static_cast<std::uint8_t>(alpha)};
            }
        }
        return true;
    }

    static bool paint_clipped(FrameBuffer& target, Rect rect, Rect clip, std::uint32_t handle,
                              ObjectFit fit, ObjectPosition position, ImageRendering rendering, void* raw) {
        const FractionalSampling sampling{{0, 1}, {0, 1}, {rect.width, 1}, {rect.height, 1}};
        return paint(target, ImagePaintFractionalRequest{rect, rect, sampling, clip,
                     handle, fit, position, rendering}, raw);
    }

    ImagePainter painter() {
        ImagePainter result;
        result.context = this;
        result.writes_only_within_rect = true;
        result.paint_clipped = paint_clipped;
        result.supports_tiled_replay = true;
        result.paint_fractional_clipped = paint;
        return result;
    }
};
#endif

struct LayoutBounds {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool valid = false;
};

struct HorizontalOverflowOffender {
    const LayoutBox* box = nullptr;
    int overflow_left = 0;
    int overflow_right = 0;
    std::size_t depth = 0;
    int area = 0;
};

struct VerticalOverflowOffender {
    const LayoutBox* box = nullptr;
    int overflow_top = 0;
    int overflow_bottom = 0;
    std::size_t depth = 0;
    int area = 0;
};

struct PipelineTimings {
    long long read_input_us = 0;
    long long parse_html_us = 0;
    long long load_parse_css_us = 0;
    long long build_render_tree_us = 0;
    long long layout_us = 0;
    long long build_layer_tree_us = 0;
    long long flatten_us = 0;
    long long paint_us = 0;
    long long present_us = 0;
    long long statistics_us = 0;
    long long total_us = 0;
};

using Clock = std::chrono::steady_clock;

long long elapsed_us(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

HostBudgets desktop_validation_budgets() {
    HostBudgets budgets;
    budgets.max_resource_bytes = kMaxInputBytes;
    budgets.max_framebuffer_pixels = 1600 * 1600;
    native::admit_desktop_core_layers(budgets);
    return budgets;
}

int parse_int_arg(const char* value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

const Node* find_first_element(const Node& node, const char* tag_name) {
    if (node.type == NodeType::Element && node.tag_name == tag_name) {
        return &node;
    }
    for (const auto& child : node.children) {
        const Node* found = find_first_element(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

Color page_background_color(const Node& document, const StyleResolver& resolver) {
    const Node* body = find_first_element(document, "body");
    if (body != nullptr) {
        const Style style = resolver.resolve(*body);
        if (style.background_color.a != 0) {
            return style.background_color;
        }
    }
    const Node* html = find_first_element(document, "html");
    if (html != nullptr) {
        const Style style = resolver.resolve(*html);
        if (style.background_color.a != 0) {
            return style.background_color;
        }
    }
    return Color{255, 255, 255, 255};
}

void accumulate_layout_bounds(const LayoutBox& box, LayoutBounds& bounds) {
    const int left = box.rect.x;
    const int top = box.rect.y;
    const int right = box.rect.x + box.rect.width;
    const int bottom = box.rect.y + box.rect.height;
    if (!bounds.valid) {
        bounds = LayoutBounds{left, top, right, bottom, true};
    } else {
        bounds.left = std::min(bounds.left, left);
        bounds.top = std::min(bounds.top, top);
        bounds.right = std::max(bounds.right, right);
        bounds.bottom = std::max(bounds.bottom, bottom);
    }
    for (const auto& child : box.children) {
        if (child) {
            accumulate_layout_bounds(*child, bounds);
        }
    }
}

void accumulate_display_bounds(const DisplayList& display_list, LayoutBounds& bounds) {
    for (const DisplayCommand& command : display_list) {
        Rect rect = command.rect;
        const auto type = native::display_command_type(command);
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION)
        if (type == DisplayCommandType::LinearGradient ||
            type == DisplayCommandType::RadialGradient ||
            type == DisplayCommandType::ConicGradient) {
            const BackgroundPaintClip clip = command.basic_paint().background_paint_clip;
            if (clip.width > 0 && clip.height > 0) {
                const std::int64_t clip_left = static_cast<std::int64_t>(rect.x) + clip.dx;
                const std::int64_t clip_top = static_cast<std::int64_t>(rect.y) + clip.dy;
                const std::int64_t left = std::max<std::int64_t>(rect.x, clip_left);
                const std::int64_t top = std::max<std::int64_t>(rect.y, clip_top);
                const std::int64_t right = std::min<std::int64_t>(
                    static_cast<std::int64_t>(rect.x) + rect.width, clip_left + clip.width);
                const std::int64_t bottom = std::min<std::int64_t>(
                    static_cast<std::int64_t>(rect.y) + rect.height, clip_top + clip.height);
                rect = right > left && bottom > top
                    ? Rect{static_cast<int>(left), static_cast<int>(top),
                           static_cast<int>(right - left), static_cast<int>(bottom - top)}
                    : Rect{};
            }
        }
#else
        (void)type;
#endif
        if (rect.width <= 0 || rect.height <= 0) {
            continue;
        }
        const int left = rect.x;
        const int top = rect.y;
        const int right = rect.x + rect.width;
        const int bottom = rect.y + rect.height;
        if (!bounds.valid) {
            bounds = LayoutBounds{left, top, right, bottom, true};
        } else {
            bounds.left = std::min(bounds.left, left);
            bounds.top = std::min(bounds.top, top);
            bounds.right = std::max(bounds.right, right);
            bounds.bottom = std::max(bounds.bottom, bottom);
        }
    }
}

void find_horizontal_overflow_offender(const LayoutBox& box,
                                       int viewport_width,
                                       std::size_t depth,
                                       HorizontalOverflowOffender& offender) {
    const int left = box.rect.x;
    const int right = box.rect.x + box.rect.width;
    const int overflow_left = std::max(0, -left);
    const int overflow_right = std::max(0, right - viewport_width);
    const int score = overflow_left + overflow_right;
    if (score > 0 && box.node != nullptr) {
        const int area = std::max(0, box.rect.width) * std::max(0, box.rect.height);
        const int current_score = offender.overflow_left + offender.overflow_right;
        if (offender.box == nullptr ||
            score > current_score ||
            (score == current_score && depth > offender.depth) ||
            (score == current_score && depth == offender.depth && area < offender.area)) {
            offender = HorizontalOverflowOffender{&box, overflow_left, overflow_right, depth, area};
        }
    }
    for (const auto& child : box.children) {
        if (child) {
            find_horizontal_overflow_offender(*child, viewport_width, depth + 1, offender);
        }
    }
}

void find_vertical_overflow_offender(const LayoutBox& box,
                                     int content_height,
                                     std::size_t depth,
                                     VerticalOverflowOffender& offender) {
    const int top = box.rect.y;
    const int bottom = box.rect.y + box.rect.height;
    const int overflow_top = std::max(0, -top);
    const int overflow_bottom = std::max(0, bottom - content_height);
    const int score = overflow_top + overflow_bottom;
    if (score > 0 && box.node != nullptr) {
        const int area = std::max(0, box.rect.width) * std::max(0, box.rect.height);
        const int current_score = offender.overflow_top + offender.overflow_bottom;
        if (offender.box == nullptr ||
            score > current_score ||
            (score == current_score && depth > offender.depth) ||
            (score == current_score && depth == offender.depth && area < offender.area)) {
            offender = VerticalOverflowOffender{&box, overflow_top, overflow_bottom, depth, area};
        }
    }
    for (const auto& child : box.children) {
        if (child) {
            find_vertical_overflow_offender(*child, content_height, depth + 1, offender);
        }
    }
}

int scroll_container_content_bottom(const LayoutBox& box) {
    int bottom = box.rect.y;
    for (const auto& child : box.children) {
        if (!child) {
            continue;
        }
        bottom = std::max(bottom, child->rect.y + child->rect.height + child->style.margin.bottom);
    }
    return bottom;
}

std::string quote_detail_value(const std::string& value, std::size_t max_chars = 160) {
    std::string output;
    output.reserve(std::min(value.size(), max_chars) + 2);
    output += '"';
    for (const char ch : value) {
        if (output.size() >= max_chars + 1) {
            break;
        }
        if (ch == '\\' || ch == '"') {
            output += '\\';
        }
        output += ch;
    }
    output += '"';
    return output;
}

bool is_vertical_scroll_container(const LayoutBox& box) {
    return (box.style.overflow == "auto" || box.style.overflow == "scroll") && box.rect.height > 0;
}

int scroll_container_overflow_y(const LayoutBox& box) {
    if (!is_vertical_scroll_container(box)) {
        return 0;
    }
    return std::max(0, scroll_container_content_bottom(box) - (box.rect.y + box.rect.height));
}

void report_scroll_container_diagnostics(const LayoutBox& box,
                                         VectorDiagnosticSink& diagnostics,
                                         const LayoutBox* scrollable_ancestor = nullptr) {
    const int overflow_px = scroll_container_overflow_y(box);
    const LayoutBox* next_scrollable_ancestor = scrollable_ancestor;
    if (overflow_px > 0) {
        std::ostringstream detail;
        detail << "node=" << quote_detail_value(dom_node_label(box.node), 48)
               << " path=" << quote_detail_value(dom_node_path(box.node), 160)
               << " boxHeight=" << box.rect.height
               << " contentHeight=" << (box.rect.height + overflow_px)
               << " overflowY=" << overflow_px;
        report_diagnostic(&diagnostics,
                          DiagnosticStage::Layout,
                          DiagnosticSeverity::Info,
                          "visual-scroll-container",
                          "Scrollable container has clipped vertical content",
                          detail.str());

        if (scrollable_ancestor != nullptr) {
            const int ancestor_overflow_y = scroll_container_overflow_y(*scrollable_ancestor);
            std::ostringstream nested_detail;
            nested_detail << "node=" << quote_detail_value(dom_node_label(box.node), 48)
                          << " path=" << quote_detail_value(dom_node_path(box.node), 160)
                          << " ancestorNode=" << quote_detail_value(dom_node_label(scrollable_ancestor->node), 48)
                          << " ancestorPath=" << quote_detail_value(dom_node_path(scrollable_ancestor->node), 160)
                          << " boxHeight=" << box.rect.height
                          << " contentHeight=" << (box.rect.height + overflow_px)
                          << " overflowY=" << overflow_px
                          << " ancestorBoxHeight=" << scrollable_ancestor->rect.height
                          << " ancestorContentHeight=" << (scrollable_ancestor->rect.height + ancestor_overflow_y)
                          << " ancestorOverflowY=" << ancestor_overflow_y;
            report_diagnostic(&diagnostics,
                              DiagnosticStage::Layout,
                              DiagnosticSeverity::Info,
                              "visual-nested-scroll-container",
                              "Nested scroll containers compete for vertical gesture input",
                              nested_detail.str());
        }
        next_scrollable_ancestor = &box;
    }
    for (const auto& child : box.children) {
        if (child) {
            report_scroll_container_diagnostics(*child, diagnostics, next_scrollable_ancestor);
        }
    }
}

std::string bounds_detail(const char* name, const LayoutBounds& bounds) {
    if (!bounds.valid) {
        return std::string(name) + "=empty";
    }
    std::ostringstream detail;
    detail << name << "=(" << bounds.left << ',' << bounds.top << ")-("
           << bounds.right << ',' << bounds.bottom << ')';
    return detail.str();
}

std::string horizontal_overflow_detail(const LayoutBounds& bounds,
                                       int viewport_width,
                                       int viewport_height,
                                       const HorizontalOverflowOffender& offender) {
    std::ostringstream detail;
    detail << bounds_detail("paintBounds", bounds)
           << " viewport=" << viewport_width << 'x' << viewport_height
           << " overflowLeft=" << (bounds.valid ? std::max(0, -bounds.left) : 0)
           << " overflowRight=" << (bounds.valid ? std::max(0, bounds.right - viewport_width) : 0);
    if (offender.box != nullptr) {
        detail << " node=" << quote_detail_value(dom_node_label(offender.box->node), 48)
               << " path=" << quote_detail_value(dom_node_path(offender.box->node), 160)
               << " boxLeft=" << offender.box->rect.x
               << " boxRight=" << (offender.box->rect.x + offender.box->rect.width)
               << " boxWidth=" << offender.box->rect.width
               << " boxOverflowLeft=" << offender.overflow_left
               << " boxOverflowRight=" << offender.overflow_right;
    }
    return detail.str();
}

std::string vertical_overflow_detail(const LayoutBounds& bounds,
                                     int content_height,
                                     const VerticalOverflowOffender& offender) {
    std::ostringstream detail;
    detail << bounds_detail("paintBounds", bounds)
           << " contentHeight=" << content_height;
    if (offender.box != nullptr) {
        detail << " node=" << quote_detail_value(dom_node_label(offender.box->node), 48)
               << " path=" << quote_detail_value(dom_node_path(offender.box->node), 160)
               << " boxTop=" << offender.box->rect.y
               << " boxBottom=" << (offender.box->rect.y + offender.box->rect.height)
               << " boxHeight=" << offender.box->rect.height
               << " boxOverflowTop=" << offender.overflow_top
               << " boxOverflowBottom=" << offender.overflow_bottom;
    }
    return detail.str();
}

void report_visual_diagnostics(const BrowserOptions& options,
                               const PipelineStatistics& statistics,
                               const LayoutBox& layout_tree,
                               const LayoutBounds& layout_bounds,
                               const LayoutBounds& paint_bounds,
                               VectorDiagnosticSink& diagnostics) {
    const int content_height = layout_bounds.valid
        ? std::max(options.viewport_height, layout_bounds.bottom)
        : options.viewport_height;
    if (paint_bounds.valid && (paint_bounds.left < 0 || paint_bounds.right > options.viewport_width)) {
        HorizontalOverflowOffender offender;
        find_horizontal_overflow_offender(layout_tree, options.viewport_width, 0, offender);
        report_diagnostic(&diagnostics,
                          DiagnosticStage::Layout,
                          DiagnosticSeverity::Warning,
                          "visual-horizontal-overflow",
                          "Paint output extends outside the viewport horizontally",
                          horizontal_overflow_detail(paint_bounds,
                                                     options.viewport_width,
                                                     options.viewport_height,
                                                     offender));
    }
    if (paint_bounds.valid && (paint_bounds.top < 0 || paint_bounds.bottom > content_height)) {
        VerticalOverflowOffender offender;
        find_vertical_overflow_offender(layout_tree, content_height, 0, offender);
        report_diagnostic(&diagnostics,
                          DiagnosticStage::Layout,
                          DiagnosticSeverity::Warning,
                          "visual-vertical-paint-overflow",
                          "Paint output extends outside the resolved content bounds vertically",
                          vertical_overflow_detail(paint_bounds, content_height, offender));
    }
    if (content_height > options.viewport_height) {
        report_diagnostic(&diagnostics,
                          DiagnosticStage::Layout,
                          DiagnosticSeverity::Info,
                          "visual-scroll-needed",
                          "Content is taller than the viewport and requires scrolling",
                          "contentHeight=" + std::to_string(content_height) +
                              " viewportHeight=" + std::to_string(options.viewport_height));
    }
    report_scroll_container_diagnostics(layout_tree, diagnostics);

    const std::size_t viewport_area = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::max(1, options.viewport_width)) *
            static_cast<std::size_t>(std::max(1, options.viewport_height)));
    const std::size_t density_limit =
        std::max<std::size_t>(512, viewport_area / 48);
    if (statistics.flattened_display_commands > density_limit) {
        const std::size_t kilopixel_units = std::max<std::size_t>(1, viewport_area / 1000);
        report_diagnostic(&diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "visual-display-command-density",
                          "Display command density is high for a small embedded viewport",
                          "flattenedDisplayCommands=" + std::to_string(statistics.flattened_display_commands) +
                              " densityLimit=" + std::to_string(density_limit) +
                              " viewportPixels=" + std::to_string(viewport_area) +
                              " commandsPerKPixel=" +
                                  std::to_string(statistics.flattened_display_commands / kilopixel_units));
    }
}

bool write_image_frame_sink(const HostFrameBufferView& frame,
                            const Rect*,
                            std::size_t,
                            void* context) {
    auto* image_context = static_cast<ImageFrameSinkContext*>(context);
    if (image_context == nullptr || frame.pixels == nullptr || frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    FrameBuffer frame_buffer;
    frame_buffer.width = frame.width;
    frame_buffer.height = frame.height;
    frame_buffer.pixels.assign(frame.pixels,
                               frame.pixels + static_cast<std::size_t>(frame.height * frame.stride_pixels));
    if (frame.stride_pixels != frame.width) {
        FrameBuffer compact(frame.width, frame.height, Color{0, 0, 0, 0});
        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                compact.pixel(x, y) = frame.pixels[static_cast<std::size_t>(y * frame.stride_pixels + x)];
            }
        }
        frame_buffer = std::move(compact);
    }
    write_image(frame_buffer, image_context->path);
    image_context->ok = true;
    return true;
}

bool is_diagnostics_json_flag(const std::string& value) {
    return value == "--diagnostics-json";
}

std::string json_escape(const std::string& value) {
    std::string output;
    output.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20U) {
                constexpr char digits[] = "0123456789abcdef";
                output += "\\u00";
                output.push_back(digits[(static_cast<unsigned char>(ch) >> 4U) & 0x0fU]);
                output.push_back(digits[static_cast<unsigned char>(ch) & 0x0fU]);
            } else {
                output.push_back(ch);
            }
            break;
        }
    }
    return output;
}

void write_diagnostics_json(const std::string& path,
                            const BrowserOptions& options,
                            const char* text_policy,
                            const PipelineStatistics& statistics,
                            const LayoutBounds& layout_bounds,
                            const LayoutBounds& paint_bounds,
                            const PipelineTimings& timings,
                            const VectorDiagnosticSink& diagnostics) {
    if (path.empty()) {
        return;
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open diagnostics JSON output");
    }

    std::size_t info_count = 0;
    std::size_t warning_count = 0;
    std::size_t error_count = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            ++error_count;
        } else if (diagnostic.severity == DiagnosticSeverity::Warning) {
            ++warning_count;
        } else {
            ++info_count;
        }
    }

    output << "{\n";
    output << "  \"format\": \"jellyframe.pipeline.diagnostics\",\n";
    output << "  \"formatVersion\": 0,\n";
    output << "  \"tool\": \"jellyframe_pseudo_browser\",\n";
    output << "  \"mode\": \"render_core\",\n";
    output << "  \"textPolicy\": \"" << text_policy << "\",\n";
    output << "  \"html\": \"" << json_escape(options.html_path) << "\",\n";
    output << "  \"css\": \"" << json_escape(options.css_path) << "\",\n";
    output << "  \"output\": \"" << json_escape(options.output_path) << "\",\n";
    output << "  \"viewport\": {\"width\": " << options.viewport_width
           << ", \"height\": " << options.viewport_height << "},\n";
    const int content_height = layout_bounds.valid
        ? std::max(options.viewport_height, layout_bounds.bottom)
        : options.viewport_height;
    const bool horizontal_overflow = paint_bounds.valid &&
        (paint_bounds.left < 0 || paint_bounds.right > options.viewport_width);
    const bool vertical_overflow = content_height > options.viewport_height;
    output << "  \"layout\": {\n";
    output << "    \"contentHeight\": " << content_height << ",\n";
    output << "    \"bounds\": {\"left\": " << (layout_bounds.valid ? layout_bounds.left : 0)
           << ", \"top\": " << (layout_bounds.valid ? layout_bounds.top : 0)
           << ", \"right\": " << (layout_bounds.valid ? layout_bounds.right : 0)
           << ", \"bottom\": " << (layout_bounds.valid ? layout_bounds.bottom : 0) << "},\n";
    output << "    \"paintBounds\": {\"left\": " << (paint_bounds.valid ? paint_bounds.left : 0)
           << ", \"top\": " << (paint_bounds.valid ? paint_bounds.top : 0)
           << ", \"right\": " << (paint_bounds.valid ? paint_bounds.right : 0)
           << ", \"bottom\": " << (paint_bounds.valid ? paint_bounds.bottom : 0) << "},\n";
    output << "    \"horizontalOverflow\": " << (horizontal_overflow ? "true" : "false") << ",\n";
    output << "    \"verticalOverflow\": " << (vertical_overflow ? "true" : "false") << "\n";
    output << "  },\n";
    output << "  \"frameUpdate\": {\"action\": \""
           << frame_update_action_name(FrameUpdateAction::RebuildPipeline)
           << "\", \"repaint\": \"" << frame_dirty_rect_mode_name(FrameDirtyRectMode::FullFrame)
           << "\", \"reason\": \"" << frame_update_reason_name(FrameUpdateReason::FirstPaint)
           << "\"},\n";
    output << "  \"timingsUs\": {\n";
    output << "    \"readInput\": " << timings.read_input_us << ",\n";
    output << "    \"parseHtml\": " << timings.parse_html_us << ",\n";
    output << "    \"loadParseCss\": " << timings.load_parse_css_us << ",\n";
    output << "    \"buildRenderTree\": " << timings.build_render_tree_us << ",\n";
    output << "    \"layout\": " << timings.layout_us << ",\n";
    output << "    \"buildLayerTree\": " << timings.build_layer_tree_us << ",\n";
    output << "    \"flatten\": " << timings.flatten_us << ",\n";
    output << "    \"paint\": " << timings.paint_us << ",\n";
    output << "    \"present\": " << timings.present_us << ",\n";
    output << "    \"statistics\": " << timings.statistics_us << ",\n";
    output << "    \"total\": " << timings.total_us << "\n";
    output << "  },\n";
    output << "  \"pipeline\": {\n";
    output << "    \"domNodes\": " << statistics.dom.node_count << ",\n";
    output << "    \"renderObjects\": " << statistics.render_objects << ",\n";
    output << "    \"layoutBoxes\": " << statistics.layout_boxes << ",\n";
    output << "    \"layers\": " << statistics.layers << ",\n";
    output << "    \"displayCommands\": " << statistics.display_commands << ",\n";
    output << "    \"flattenedDisplayCommands\": " << statistics.flattened_display_commands << ",\n";
    output << "    \"framebufferBytes\": " << statistics.framebuffer_bytes << ",\n";
    output << "    \"resourceBytes\": " << statistics.resource_bytes << ",\n";
    output << "    \"estimatedHeapBytes\": " << statistics.estimated_heap_bytes << "\n";
    output << "  },\n";
    output << "  \"summary\": {\"total\": " << diagnostics.size()
           << ", \"info\": " << info_count
           << ", \"warning\": " << warning_count
           << ", \"error\": " << error_count << "},\n";
    output << "  \"diagnostics\": [\n";
    const auto& entries = diagnostics.diagnostics();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Diagnostic& diagnostic = entries[i];
        output << "    {\"stage\": \"" << diagnostic_stage_name(diagnostic.stage)
               << "\", \"severity\": \"" << diagnostic_severity_name(diagnostic.severity)
               << "\", \"code\": \"" << json_escape(diagnostic.code)
               << "\", \"message\": \"" << json_escape(diagnostic.message)
               << "\", \"detail\": \"" << json_escape(diagnostic.detail) << "\"}";
        if (i + 1 < entries.size()) {
            output << ',';
        }
        output << '\n';
    }
    output << "  ]\n";
    output << "}\n";
}

void finish_capture(const BrowserOptions& options, VectorDiagnosticSink& diagnostics,
                    PipelineTimings& timings, Clock::time_point total_start,
                    const Node& document, const RenderObject& render_tree,
                    const LayoutBox& layout_tree, const LayerNode& layer_tree,
                    const DisplayList& display_list, const FrameBuffer& frame_buffer,
                    Color background, const char* text_policy, std::size_t resource_bytes) {
    ImageFrameSinkContext frame_sink_context{options.output_path, false};
    const Rect full_dirty{0, 0, frame_buffer.width, frame_buffer.height};
    const HostFrameSink frame_sink{write_image_frame_sink, &frame_sink_context};
    auto stage_start = Clock::now();
    if (!present_frame(frame_buffer, frame_sink, &full_dirty, 1)) {
        throw std::runtime_error("failed to present output frame");
    }
    timings.present_us = elapsed_us(stage_start, Clock::now());

    stage_start = Clock::now();
    const PipelineStatistics pipeline_statistics = collect_pipeline_statistics(PipelineStatisticsInput{
        &document,
        &render_tree,
        &layout_tree,
        &layer_tree,
        &display_list,
        &frame_buffer,
        nullptr,
        nullptr,
        nullptr,
        resource_bytes,
    });
    timings.statistics_us = elapsed_us(stage_start, Clock::now());

    LayoutBounds layout_bounds;
    accumulate_layout_bounds(layout_tree, layout_bounds);
    LayoutBounds paint_bounds;
    accumulate_display_bounds(display_list, paint_bounds);
    report_visual_diagnostics(options, pipeline_statistics, layout_tree, layout_bounds, paint_bounds, diagnostics);
    timings.total_us = elapsed_us(total_start, Clock::now());

    std::cout << "JellyFrame render core pseudo browser\n";
    std::cout << "  output=" << options.output_path << '\n';
    std::cout << "  text_policy=" << text_policy << '\n';
    std::cout << "  viewport=" << options.viewport_width << "x" << options.viewport_height << '\n';
    std::cout << "  dom_nodes=" << pipeline_statistics.dom.node_count << '\n';
    std::cout << "  dom_max_depth=" << pipeline_statistics.dom.max_depth << '\n';
    std::cout << "  dom_attributes=" << pipeline_statistics.dom.attribute_count << '\n';
    std::cout << "  render_objects=" << pipeline_statistics.render_objects << '\n';
    std::cout << "  layout_boxes=" << pipeline_statistics.layout_boxes << '\n';
    std::cout << "  layers=" << pipeline_statistics.layers << '\n';
    std::cout << "  display_commands=" << pipeline_statistics.flattened_display_commands << '\n';
    std::cout << "  layer_display_commands=" << pipeline_statistics.display_commands << '\n';
    std::cout << "  framebuffer_bytes=" << pipeline_statistics.framebuffer_bytes << '\n';
    std::cout << "  resource_bytes=" << pipeline_statistics.resource_bytes << '\n';
    std::cout << "  estimated_pipeline_bytes=" << pipeline_statistics.estimated_heap_bytes << '\n';
    std::cout << "  timing_total_us=" << timings.total_us << '\n';
    std::cout << "  timing_paint_us=" << timings.paint_us << '\n';
    std::cout << "  frame_sink=" << (frame_sink_context.ok ? "image" : "none") << '\n';
    std::cout << "  non_background_pixels=" << count_non_background_pixels(frame_buffer, background) << '\n';
    std::cout << "  diagnostics=" << diagnostics.size() << '\n';
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        std::cout << "  diagnostic [" << diagnostic_severity_name(diagnostic.severity) << "] "
                  << diagnostic_stage_name(diagnostic.stage) << "::" << diagnostic.code
                  << " - " << diagnostic.message;
        if (!diagnostic.detail.empty()) {
            std::cout << " (" << diagnostic.detail << ')';
        }
        std::cout << '\n';
    }

    write_diagnostics_json(options.diagnostics_json_path,
                           options,
                           text_policy,
                           pipeline_statistics,
                           layout_bounds,
                           paint_bounds,
                           timings,
                           diagnostics);
}

BrowserOptions parse_options(int argc, char** argv) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "usage: jellyframe_pseudo_browser page.html style.css output.ppm "
                     "[viewport_width] [viewport_height] [--diagnostics-json report.json] [--resource-root directory] [--package]\n";
        std::exit(0);
    }
    if (argc < 4) {
        throw std::runtime_error(
            "usage: jellyframe_pseudo_browser page.html style.css output.ppm "
            "[viewport_width] [viewport_height] [--diagnostics-json report.json] [--resource-root directory] [--package]");
    }

    BrowserOptions options;
    options.html_path = argv[1];
    options.css_path = argv[2];
    options.output_path = argv[3];

    bool width_set = false;
    bool height_set = false;
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--package") {
            options.package_mode = true;
            continue;
        }
        if (arg == "--resource-root") {
            if (i + 1 >= argc) throw std::runtime_error("--resource-root requires a directory");
            options.resource_root = argv[++i];
            continue;
        }
        if (is_diagnostics_json_flag(arg)) {
            if (i + 1 >= argc) {
                throw std::runtime_error("--diagnostics-json requires a file path");
            }
            options.diagnostics_json_path = argv[++i];
            continue;
        }
        if (!width_set) {
            options.viewport_width = parse_int_arg(argv[i], options.viewport_width);
            options.viewport_width_set = true;
            width_set = true;
            continue;
        }
        if (!height_set) {
            options.viewport_height = parse_int_arg(argv[i], options.viewport_height);
            options.viewport_height_set = true;
            height_set = true;
            continue;
        }
        throw std::runtime_error("unexpected extra argument: " + arg);
    }
    if (options.package_mode && options.resource_root.empty()) {
        throw std::runtime_error("--package requires --resource-root");
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto total_start = Clock::now();
        PipelineTimings timings;
        const BrowserOptions options = parse_options(argc, argv);
        const HostBudgets budgets = desktop_validation_budgets();
        VectorDiagnosticSink diagnostics;
        const std::filesystem::path html_path(options.html_path);
        jellyframe_example::PackageResourceContext package_resources;
        if (!options.resource_root.empty()) {
            package_resources.root = std::filesystem::weakly_canonical(options.resource_root);
            const auto relative = std::filesystem::weakly_canonical(html_path).lexically_relative(package_resources.root);
            if (relative.empty() || relative.is_absolute() ||
                std::any_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; }))
                throw std::runtime_error("HTML entry is outside the package resource root");
            package_resources.base_url = "/" + relative.generic_string();
            package_resources.max_input_bytes = kMaxInputBytes;
            package_resources.diagnostics = &diagnostics;
        }
        jellyframe_example::AppPackageManifest manifest;
        bool contextual = false;
        if (options.package_mode) {
            manifest = jellyframe_example::load_app_package(package_resources.root, kMaxInputBytes, true).manifest;
            contextual = !manifest.contextual_text_pipeline.empty();
#if defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME)
            if (contextual && !native::contextual_desktop_available)
#else
            if (contextual)
#endif
                throw std::runtime_error("contextual package capture is unavailable in this build");
            if (manifest.entry != package_resources.base_url)
                throw std::runtime_error("HTML entry does not match the package manifest");
        }
        const TextSystem* text_system = nullptr;
#if defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME) && \
    JELLYFRAME_NATIVE_CORE_TEXT_DEFAULT
        AppRuntimeHost font_runtime{AppRuntimeHostOptions{0, 0, 0, 0, native::kDesktopAppFontCapacity}};
        if (!contextual) {
            native::attach_desktop_system_font_catalog(font_runtime);
            if (!manifest.font_sources.empty()) {
                font_runtime.launch(manifest.id, AppRole::App);
                native::load_package_fonts_into_runtime(manifest, package_resources, font_runtime, &diagnostics);
            }
            text_system = font_runtime.fonts().core_text_system();
            if (text_system == nullptr) throw std::runtime_error("Core TextSystem initialization failed");
        }
#else
        if (!contextual && !manifest.font_sources.empty())
            throw std::runtime_error("package fonts require the Core text backend");
#endif

        HtmlParser html_parser;
        HtmlParserOptions html_options = html_parser_options_from_budgets(budgets);
        html_options.diagnostics = &diagnostics;
        auto stage_start = Clock::now();
        const std::string html = jellyframe_example::read_file_limited(options.html_path, kMaxInputBytes);
        timings.read_input_us = elapsed_us(stage_start, Clock::now());
        stage_start = Clock::now();
        auto document = html_parser.parse(html, html_options);
        timings.parse_html_us = elapsed_us(stage_start, Clock::now());

        jellyframe_example::StylesheetLoadContext stylesheet_context;
        stylesheet_context.base_dir =
            html_path.has_parent_path() ? html_path.parent_path() : std::filesystem::current_path();
        stylesheet_context.max_input_bytes = kMaxInputBytes;
        stylesheet_context.diagnostics = &diagnostics;
        stage_start = Clock::now();
        const std::string css = combine_author_css(jellyframe_example::read_file_limited(options.css_path, kMaxInputBytes),
                                                   *document,
                                                   options.resource_root.empty() ? jellyframe_example::load_linked_stylesheet :
                                                       jellyframe_example::load_package_stylesheet,
                                                   options.resource_root.empty() ? static_cast<void*>(&stylesheet_context) :
                                                       static_cast<void*>(&package_resources),
                                                   document_style_collection_options_from_budgets(budgets, &diagnostics));

        CssParser css_parser;
        CssParserOptions css_options = css_parser_options_from_budgets(budgets);
        css_options.media_viewport_width = options.viewport_width;
        css_options.media_viewport_height = options.viewport_height;
        css_options.diagnostics = &diagnostics;
        Stylesheet stylesheet = css_parser.parse(css, css_options);
        timings.load_parse_css_us = elapsed_us(stage_start, Clock::now());
        StyleResolverOptions style_options;
        style_options.diagnostics = &diagnostics;
        StyleResolver resolver(std::move(stylesheet), style_options);

        RenderTreeOptions render_options = render_tree_options_from_budgets(budgets);
        render_options.diagnostics = &diagnostics;
        render_options.viewport_width = options.viewport_width;
        render_options.viewport_height = options.viewport_height;
        const Color background = page_background_color(*document, resolver);
#if defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME) && defined(_WIN32) && \
    JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
        if (contextual) {
            const auto selected = native::load_desktop_contextual_text(manifest, package_resources);
            auto layout_options = layout_engine_options_from_budgets(budgets);
            auto layer_options = layer_tree_options_from_budgets(budgets);
            layout_options.diagnostics = &diagnostics;
            layer_options.diagnostics = &diagnostics;
            stage_start = Clock::now();
            const auto frame = native::build_desktop_contextual_frame(*document, resolver, selected,
                options.viewport_width, options.viewport_height, render_options, layout_options, layer_options);
            timings.layout_us = elapsed_us(stage_start, Clock::now());
            stage_start = Clock::now();
            const auto pixels = native::paint_desktop_contextual_frame(frame, options.viewport_width,
                options.viewport_height, background, budgets.max_framebuffer_pixels);
            timings.paint_us = elapsed_us(stage_start, Clock::now());
            const auto& layer = *frame.capture.layer_tree();
            finish_capture(options, diagnostics, timings, total_start, *document, *frame.render_tree,
                *frame.layout.tree(), layer, layer.display_list, pixels,
                background, "core-contextual-v12-local", html.size() + css.size());
            return 0;
        }
#endif
        RenderTreeBuilder render_tree_builder(resolver, render_options);
        stage_start = Clock::now();
        auto render_tree = render_tree_builder.build(*document);
        timings.build_render_tree_us = elapsed_us(stage_start, Clock::now());

#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED) && defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME)
        PseudoImages images(options, resolver, &diagnostics);
        images.prepare(*render_tree);
        NaturalImageSizingOptions natural_images;
        natural_images.provider = {PseudoImages::natural_size, &images};
#endif
        LayoutEngineOptions layout_options = layout_engine_options_from_budgets(budgets);
        layout_options.diagnostics = &diagnostics;
        layout_options.text_system = text_system;
#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED) && defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME)
        layout_options.natural_image_sizing = &natural_images;
#endif
        TextSystemStatus text_status = TextSystemStatus::Ok;
#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED)
        layout_options.text_status = &text_status;
#endif
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION)
        LayoutEngine layout_engine(resolver, layout_options);
#else
        LayoutEngine layout_engine(resolver, TextMeasureProvider{}, layout_options);
#endif
        stage_start = Clock::now();
        auto layout_tree = layout_engine.layout(*render_tree, options.viewport_width, options.viewport_height);
        if (!layout_tree) throw std::runtime_error("layout refused (Core text status=" +
            std::to_string(static_cast<unsigned>(text_status)) + ")");
        timings.layout_us = elapsed_us(stage_start, Clock::now());

        LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(budgets);
        layer_options.diagnostics = &diagnostics;
        layer_options.text_system = text_system;
#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED) && defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME)
        layer_options.image_resolver = {PseudoImages::resolve, &images, PseudoImages::dimensions};
#endif
        LayerTreeBuilder layer_tree_builder(layer_options);
        stage_start = Clock::now();
        auto layer_tree = layer_tree_builder.build(*layout_tree);
        if (!layer_tree) throw std::runtime_error("layer tree refused Core text or paint input");
        timings.build_layer_tree_us = elapsed_us(stage_start, Clock::now());
        stage_start = Clock::now();
        DisplayList display_list = layer_tree_builder.flatten(*layer_tree);
        timings.flatten_us = elapsed_us(stage_start, Clock::now());

        SoftwareCompositor::Options compositor_options = software_compositor_options_from_budgets(budgets);
        compositor_options.diagnostics = &diagnostics;
        compositor_options.text_system = text_system;
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION)
        SoftwareCompositor compositor(ImagePainter{}, compositor_options);
#else
#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED) && defined(JELLYFRAME_PSEUDO_BROWSER_HAS_APP_RUNTIME)
        SoftwareCompositor compositor(TextPainter{}, images.painter(), compositor_options);
#else
        SoftwareCompositor compositor(TextPainter{}, ImagePainter{}, compositor_options);
#endif
#endif
        stage_start = Clock::now();
        FrameBuffer frame_buffer = native::paint_frame(compositor, *layer_tree, options.viewport_width,
            options.viewport_height, background, budgets.max_framebuffer_pixels);
        timings.paint_us = elapsed_us(stage_start, Clock::now());
        if (frame_buffer.width <= 0 || frame_buffer.height <= 0) {
            throw std::runtime_error("framebuffer budget exceeded");
        }

        finish_capture(options, diagnostics, timings, total_start, *document, *render_tree,
            *layout_tree, *layer_tree, display_list, frame_buffer, background,
            text_system ? "core-text" : "geometry-only", html.size() + css.size());
    } catch (const std::exception& error) {
        std::cerr << "pseudo browser failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
