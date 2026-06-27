#include "render_core/budget.h"
#include "render_core/css_parser.h"
#include "render_core/diagnostics.h"
#include "render_core/document_style.h"
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
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

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

std::string bounds_detail(const char* name, const LayoutBounds& bounds) {
    if (!bounds.valid) {
        return std::string(name) + "=empty";
    }
    std::ostringstream detail;
    detail << name << "=(" << bounds.left << ',' << bounds.top << ")-("
           << bounds.right << ',' << bounds.bottom << ')';
    return detail.str();
}

void report_visual_diagnostics(const BrowserOptions& options,
                               const PipelineStatistics& statistics,
                               const LayoutBounds& layout_bounds,
                               const LayoutBounds& paint_bounds,
                               VectorDiagnosticSink& diagnostics) {
    const int content_height = layout_bounds.valid
        ? std::max(options.viewport_height, layout_bounds.bottom)
        : options.viewport_height;
    if (paint_bounds.valid && (paint_bounds.left < 0 || paint_bounds.right > options.viewport_width)) {
        report_diagnostic(&diagnostics,
                          DiagnosticStage::Layout,
                          DiagnosticSeverity::Warning,
                          "visual-horizontal-overflow",
                          "Paint output extends outside the viewport horizontally",
                          bounds_detail("paintBounds", paint_bounds));
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

    const int viewport_area = std::max(1, options.viewport_width * options.viewport_height);
    const std::size_t density_limit =
        std::max<std::size_t>(512, static_cast<std::size_t>(viewport_area / 48));
    if (statistics.flattened_display_commands > density_limit) {
        report_diagnostic(&diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "visual-display-command-density",
                          "Display command density is high for a small embedded viewport",
                          "flattenedDisplayCommands=" + std::to_string(statistics.flattened_display_commands) +
                              " densityLimit=" + std::to_string(density_limit));
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
        const BrowserOptions options = parse_options(argc, argv);
        const HostBudgets budgets = desktop_validation_budgets();
        VectorDiagnosticSink diagnostics;

        HtmlParser html_parser;
        HtmlParserOptions html_options = html_parser_options_from_budgets(budgets);
        html_options.diagnostics = &diagnostics;
        const std::string html = jellyframe_example::read_file_limited(options.html_path, kMaxInputBytes);
        auto document = html_parser.parse(html, html_options);

        jellyframe_example::StylesheetLoadContext stylesheet_context;
        const std::filesystem::path html_path(options.html_path);
        stylesheet_context.base_dir =
            html_path.has_parent_path() ? html_path.parent_path() : std::filesystem::current_path();
        stylesheet_context.max_input_bytes = kMaxInputBytes;
        stylesheet_context.diagnostics = &diagnostics;
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
        StyleResolverOptions style_options;
        style_options.diagnostics = &diagnostics;
        StyleResolver resolver(std::move(stylesheet), style_options);

        RenderTreeOptions render_options = render_tree_options_from_budgets(budgets);
        render_options.diagnostics = &diagnostics;
        RenderTreeBuilder render_tree_builder(resolver, render_options);
        auto render_tree = render_tree_builder.build(*document);

        LayoutEngineOptions layout_options = layout_engine_options_from_budgets(budgets);
        layout_options.diagnostics = &diagnostics;
        LayoutEngine layout_engine(resolver, {}, layout_options);
        auto layout_tree = layout_engine.layout(*render_tree, options.viewport_width, options.viewport_height);

        LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(budgets);
        layer_options.diagnostics = &diagnostics;
        LayerTreeBuilder layer_tree_builder(layer_options);
        auto layer_tree = layer_tree_builder.build(*layout_tree);
        DisplayList display_list = layer_tree_builder.flatten(*layer_tree);

        SoftwareCompositor::Options compositor_options = software_compositor_options_from_budgets(budgets);
        compositor_options.diagnostics = &diagnostics;
        SoftwareCompositor compositor({}, compositor_options);
        const Color background = page_background_color(*document, resolver);
        FrameBuffer frame_buffer = compositor.render(*layer_tree, options.viewport_width, options.viewport_height, background);
        if (frame_buffer.width <= 0 || frame_buffer.height <= 0) {
            throw std::runtime_error("framebuffer budget exceeded");
        }

        ImageFrameSinkContext frame_sink_context{options.output_path, false};
        const Rect full_dirty{0, 0, frame_buffer.width, frame_buffer.height};
        const HostFrameSink frame_sink{write_image_frame_sink, &frame_sink_context};
        if (!present_frame(frame_buffer, frame_sink, &full_dirty, 1)) {
            throw std::runtime_error("failed to present output frame");
        }

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

        LayoutBounds layout_bounds;
        accumulate_layout_bounds(*layout_tree, layout_bounds);
        LayoutBounds paint_bounds;
        accumulate_display_bounds(display_list, paint_bounds);
        report_visual_diagnostics(options, pipeline_statistics, layout_bounds, paint_bounds, diagnostics);

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
                               diagnostics);
    } catch (const std::exception& error) {
        std::cerr << "pseudo browser failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
