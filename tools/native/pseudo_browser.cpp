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
    std::string diagnostics_json_path;
    int viewport_width = 360;
    int viewport_height = 240;
    bool viewport_width_set = false;
    bool viewport_height_set = false;
};

struct ImageFrameSinkContext {
    std::string path;
    bool ok = false;
};

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
        const Rect& rect = command.rect;
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

void report_scroll_container_diagnostics(const LayoutBox& box, VectorDiagnosticSink& diagnostics) {
    if ((box.style.overflow == "auto" || box.style.overflow == "scroll") && box.rect.height > 0) {
        const int content_bottom = scroll_container_content_bottom(box);
        const int overflow_px = content_bottom - (box.rect.y + box.rect.height);
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
        }
    }
    for (const auto& child : box.children) {
        if (child) {
            report_scroll_container_diagnostics(*child, diagnostics);
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

BrowserOptions parse_options(int argc, char** argv) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "usage: jellyframe_pseudo_browser page.html style.css output.ppm "
                     "[viewport_width] [viewport_height] [--diagnostics-json report.json]\n";
        std::exit(0);
    }
    if (argc < 4) {
        throw std::runtime_error(
            "usage: jellyframe_pseudo_browser page.html style.css output.ppm "
            "[viewport_width] [viewport_height] [--diagnostics-json report.json]");
    }

    BrowserOptions options;
    options.html_path = argv[1];
    options.css_path = argv[2];
    options.output_path = argv[3];

    bool width_set = false;
    bool height_set = false;
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
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
        const std::filesystem::path html_path(options.html_path);
        stylesheet_context.base_dir =
            html_path.has_parent_path() ? html_path.parent_path() : std::filesystem::current_path();
        stylesheet_context.max_input_bytes = kMaxInputBytes;
        stylesheet_context.diagnostics = &diagnostics;
        stage_start = Clock::now();
        const std::string css = combine_author_css(jellyframe_example::read_file_limited(options.css_path, kMaxInputBytes),
                                                   *document,
                                                   jellyframe_example::load_linked_stylesheet,
                                                   &stylesheet_context);

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
        RenderTreeBuilder render_tree_builder(resolver, render_options);
        stage_start = Clock::now();
        auto render_tree = render_tree_builder.build(*document);
        timings.build_render_tree_us = elapsed_us(stage_start, Clock::now());

        LayoutEngineOptions layout_options = layout_engine_options_from_budgets(budgets);
        layout_options.diagnostics = &diagnostics;
        LayoutEngine layout_engine(resolver, {}, layout_options);
        stage_start = Clock::now();
        auto layout_tree = layout_engine.layout(*render_tree, options.viewport_width, options.viewport_height);
        timings.layout_us = elapsed_us(stage_start, Clock::now());

        LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(budgets);
        layer_options.diagnostics = &diagnostics;
        LayerTreeBuilder layer_tree_builder(layer_options);
        stage_start = Clock::now();
        auto layer_tree = layer_tree_builder.build(*layout_tree);
        timings.build_layer_tree_us = elapsed_us(stage_start, Clock::now());
        stage_start = Clock::now();
        DisplayList display_list = layer_tree_builder.flatten(*layer_tree);
        timings.flatten_us = elapsed_us(stage_start, Clock::now());

        SoftwareCompositor::Options compositor_options = software_compositor_options_from_budgets(budgets);
        compositor_options.diagnostics = &diagnostics;
        SoftwareCompositor compositor({}, compositor_options);
        const Color background = page_background_color(*document, resolver);
        stage_start = Clock::now();
        FrameBuffer frame_buffer = compositor.render(*layer_tree, options.viewport_width, options.viewport_height, background);
        timings.paint_us = elapsed_us(stage_start, Clock::now());
        if (frame_buffer.width <= 0 || frame_buffer.height <= 0) {
            throw std::runtime_error("framebuffer budget exceeded");
        }

        ImageFrameSinkContext frame_sink_context{options.output_path, false};
        const Rect full_dirty{0, 0, frame_buffer.width, frame_buffer.height};
        const HostFrameSink frame_sink{write_image_frame_sink, &frame_sink_context};
        stage_start = Clock::now();
        if (!present_frame(frame_buffer, frame_sink, &full_dirty, 1)) {
            throw std::runtime_error("failed to present output frame");
        }
        timings.present_us = elapsed_us(stage_start, Clock::now());

        stage_start = Clock::now();
        const PipelineStatistics pipeline_statistics = collect_pipeline_statistics(PipelineStatisticsInput{
            document.get(),
            render_tree.get(),
            layout_tree.get(),
            layer_tree.get(),
            &display_list,
            &frame_buffer,
            nullptr,
            nullptr,
            nullptr,
            html.size() + css.size(),
        });
        timings.statistics_us = elapsed_us(stage_start, Clock::now());

        LayoutBounds layout_bounds;
        accumulate_layout_bounds(*layout_tree, layout_bounds);
        LayoutBounds paint_bounds;
        accumulate_display_bounds(display_list, paint_bounds);
        report_visual_diagnostics(options, pipeline_statistics, *layout_tree, layout_bounds, paint_bounds, diagnostics);
        timings.total_us = elapsed_us(total_start, Clock::now());

        std::cout << "JellyFrame render core pseudo browser\n";
        std::cout << "  output=" << options.output_path << '\n';
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
                               pipeline_statistics,
                               layout_bounds,
                               paint_bounds,
                               timings,
                               diagnostics);
    } catch (const std::exception& error) {
        std::cerr << "pseudo browser failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
