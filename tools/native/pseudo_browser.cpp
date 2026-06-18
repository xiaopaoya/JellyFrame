#include "core/css_parser.h"
#include "core/budget.h"
#include "core/document_script.h"
#include "core/document_style.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/pipeline_statistics.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"
#include "core/style.h"

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
#include "script/jerryscript_runtime.h"
#endif

#include "example_css_io.h"
#include "app_package.h"

#include <algorithm>
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

HostBudgets desktop_validation_budgets() {
    HostBudgets budgets;
    budgets.max_resource_bytes = kMaxInputBytes;
    budgets.max_framebuffer_pixels = 1600 * 1600;
    return budgets;
}

std::string read_file_limited(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open input file");
    }

    std::ostringstream output;
    char buffer[4096];
    std::size_t total = 0;
    while (file && total < kMaxInputBytes) {
        const std::size_t remaining = kMaxInputBytes - total;
        const std::size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        file.read(buffer, static_cast<std::streamsize>(chunk));
        const std::streamsize read = file.gcount();
        if (read <= 0) {
            break;
        }
        output.write(buffer, read);
        total += static_cast<std::size_t>(read);
    }
    return output.str();
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

struct BrowserOptions {
    std::string html_path;
    std::string css_path;
    std::string output_path;
    std::string script_path;
    std::string app_path;
    int viewport_width = 360;
    int viewport_height = 240;
    bool viewport_width_set = false;
    bool viewport_height_set = false;
    int pump_timers_ms = 0;
};

struct ImageFrameSinkContext {
    std::string path;
    bool ok = false;
};

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

bool is_script_flag(const std::string& value) {
    return value == "--script" || value == "-s";
}

bool is_pump_timers_flag(const std::string& value) {
    return value == "--pump-timers";
}

bool is_app_flag(const std::string& value) {
    return value == "--app";
}

BrowserOptions parse_options(int argc, char** argv) {
    if (argc >= 2 && is_app_flag(argv[1])) {
        if (argc < 4) {
            throw std::runtime_error(
                "usage: jellyframe_pseudo_browser --app package_dir output.ppm [viewport_width] [viewport_height] "
                "[--script script.js] [--pump-timers ms]");
        }
        BrowserOptions options;
        options.app_path = argv[2];
        options.output_path = argv[3];
        bool width_set = false;
        bool height_set = false;
        for (int i = 4; i < argc; ++i) {
            const std::string arg = argv[i];
            if (is_script_flag(arg)) {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--script requires a script file path");
                }
                options.script_path = argv[++i];
                continue;
            }
            if (is_pump_timers_flag(arg)) {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--pump-timers requires a duration in milliseconds");
                }
                options.pump_timers_ms = std::max(0, parse_int_arg(argv[++i], 0));
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
            throw std::runtime_error("too many positional arguments");
        }
        return options;
    }

    if (argc < 4) {
        throw std::runtime_error(
            "usage: jellyframe_pseudo_browser page.html style.css output.ppm [viewport_width] [viewport_height] "
            "[--script script.js]\n"
            "   or: jellyframe_pseudo_browser --app package_dir output.ppm [viewport_width] [viewport_height]");
    }

    BrowserOptions options;
    options.html_path = argv[1];
    options.css_path = argv[2];
    options.output_path = argv[3];

    bool width_set = false;
    bool height_set = false;
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (is_script_flag(arg)) {
            if (i + 1 >= argc) {
                throw std::runtime_error("--script requires a script file path");
            }
            options.script_path = argv[++i];
            continue;
        }
        if (is_pump_timers_flag(arg)) {
            if (i + 1 >= argc) {
                throw std::runtime_error("--pump-timers requires a duration in milliseconds");
            }
            options.pump_timers_ms = std::max(0, parse_int_arg(argv[++i], 0));
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
        if (options.script_path.empty()) {
            options.script_path = arg;
            continue;
        }

        throw std::runtime_error("too many positional arguments");
    }

    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const BrowserOptions options = parse_options(argc, argv);
        const HostBudgets budgets = desktop_validation_budgets();
        std::string html;
        std::string css;
        std::string package_id;
        std::string package_name;
        bool package_network_allowed = false;
        jellyframe_example::PackageResourceStats package_stats;
        jellyframe_example::PackageResourceContext package_context;
        BrowserOptions effective_options = options;
        std::string standalone_script_source;

        if (!options.app_path.empty()) {
            const jellyframe_example::AppPackage app =
                jellyframe_example::load_app_package(options.app_path, kMaxInputBytes);
            package_id = app.manifest.id;
            package_name = app.manifest.name;
            package_network_allowed = app.manifest.network_allowed;
            if (app.manifest.viewport_width > 0 && !effective_options.viewport_width_set) {
                effective_options.viewport_width = app.manifest.viewport_width;
            }
            if (app.manifest.viewport_height > 0 && !effective_options.viewport_height_set) {
                effective_options.viewport_height = app.manifest.viewport_height;
            }
            package_context.root = app.root;
            package_context.base_url = app.manifest.entry;
            package_context.max_input_bytes = kMaxInputBytes;
            package_context.stats = &package_stats;
            if (!jellyframe_example::load_package_resource(app.manifest.entry, "/", html, &package_context)) {
                throw std::runtime_error("failed to load app entry resource");
            }
        } else {
            html = read_file_limited(options.html_path);
        }

        HtmlParser html_parser;
        CssParser css_parser;
        auto document = html_parser.parse(html, html_parser_options_from_budgets(budgets));

        bool script_ran = false;
        bool script_ok = false;
        std::string script_output;
        std::size_t timer_callbacks = 0;
        std::size_t document_script_count = 0;
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        std::unique_ptr<JerryScriptRuntime> runtime;
        ScriptRuntimeStatistics script_statistics;
        bool has_script_statistics = false;
#endif
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        std::vector<DocumentScript> document_scripts;
        if (!options.app_path.empty()) {
            document_scripts = collect_classic_scripts(*document, jellyframe_example::load_package_script, &package_context);
        } else {
            jellyframe_example::ScriptLoadContext script_context;
            const std::filesystem::path html_path(options.html_path);
            script_context.base_dir =
                html_path.has_parent_path() ? html_path.parent_path() : std::filesystem::current_path();
            script_context.max_input_bytes = kMaxInputBytes;
            document_scripts = collect_classic_scripts(*document, jellyframe_example::load_linked_script, &script_context);
        }
        document_script_count = document_scripts.size();
#endif
        if (!effective_options.script_path.empty()
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
            || !document_scripts.empty()
#endif
        ) {
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
            runtime = std::make_unique<JerryScriptRuntime>(budgets);
            runtime->set_host_time_ms(0);
            runtime->bind_document(*document);
            script_ok = true;
            for (const DocumentScript& script : document_scripts) {
                const ScriptEvaluationResult script_result = runtime->eval(script.source, script.name);
                if (!script_result.ok) {
                    script_ok = false;
                    script_output = script_result.error;
                    break;
                }
                script_output = script_result.value;
            }
            if (script_ok && !effective_options.script_path.empty()) {
                standalone_script_source = read_file_limited(effective_options.script_path);
                const ScriptEvaluationResult script_result =
                    runtime->eval(standalone_script_source, effective_options.script_path);
                script_ok = script_result.ok;
                script_output = script_result.ok ? script_result.value : script_result.error;
            }
            script_ran = true;
            for (int now_ms = 16; now_ms <= effective_options.pump_timers_ms; now_ms += 16) {
                timer_callbacks += runtime->pump_timers(static_cast<std::uint64_t>(now_ms), 32);
            }
            if (effective_options.pump_timers_ms > 0 && effective_options.pump_timers_ms % 16 != 0) {
                timer_callbacks += runtime->pump_timers(static_cast<std::uint64_t>(effective_options.pump_timers_ms), 32);
            }
            script_statistics = runtime->statistics();
            has_script_statistics = true;
#else
            throw std::runtime_error("this build was compiled without JELLYFRAME_BUILD_SCRIPTING=ON");
#endif
        }

        if (!options.app_path.empty()) {
            css = combine_author_css({}, *document, jellyframe_example::load_package_stylesheet, &package_context);
        } else {
            css = jellyframe_example::read_author_css_for_document(options.css_path, *document, kMaxInputBytes);
        }
        Stylesheet stylesheet = css_parser.parse(css, css_parser_options_from_budgets(budgets));
        StyleResolver resolver(std::move(stylesheet));

        RenderTreeBuilder render_tree_builder(resolver, render_tree_options_from_budgets(budgets));
        auto render_tree = render_tree_builder.build(*document);
        LayoutEngine layout_engine(resolver, {}, layout_engine_options_from_budgets(budgets));
        auto layout_tree = layout_engine.layout(*render_tree, effective_options.viewport_width);
        LayerTreeBuilder layer_tree_builder(layer_tree_options_from_budgets(budgets));
        auto layer_tree = layer_tree_builder.build(*layout_tree);
        DisplayList display_list = layer_tree_builder.flatten(*layer_tree);

        SoftwareCompositor compositor({}, software_compositor_options_from_budgets(budgets));
        const Color background = page_background_color(*document, resolver);
        FrameBuffer frame_buffer = compositor.render(
            *layer_tree, effective_options.viewport_width, effective_options.viewport_height, background);
        if (frame_buffer.width <= 0 || frame_buffer.height <= 0) {
            throw std::runtime_error("framebuffer budget exceeded");
        }
        ImageFrameSinkContext frame_sink_context{effective_options.output_path, false};
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
            !options.app_path.empty()
                ? package_stats.loaded_bytes
                : html.size() + css.size() + standalone_script_source.size(),
        });

        std::cout << "JellyFrame pseudo browser\n";
        std::cout << "  output=" << effective_options.output_path << '\n';
        std::cout << "  viewport=" << effective_options.viewport_width << "x" << effective_options.viewport_height << '\n';
        if (!options.app_path.empty()) {
            std::cout << "  app_id=" << package_id << '\n';
            std::cout << "  app_name=" << package_name << '\n';
            std::cout << "  app_network_allowed=" << (package_network_allowed ? "true" : "false") << '\n';
            std::cout << "  app_resource_loads=" << package_stats.successful_loads << '\n';
            std::cout << "  app_resource_missing=" << package_stats.missing_loads << '\n';
            std::cout << "  app_resource_rejected=" << package_stats.rejected_loads << '\n';
            std::cout << "  app_resource_bytes=" << package_stats.loaded_bytes << '\n';
        }
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
        std::cout << "  non_background_pixels="
                  << count_non_background_pixels(frame_buffer, background) << '\n';
        if (script_ran) {
            std::cout << "  script=" << effective_options.script_path << '\n';
            std::cout << "  document_scripts=" << document_script_count << '\n';
            std::cout << "  script_ok=" << (script_ok ? "true" : "false") << '\n';
            std::cout << "  script_result=" << script_output << '\n';
            std::cout << "  timer_callbacks=" << timer_callbacks << '\n';
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
            if (has_script_statistics) {
                std::cout << "  script_timers=" << script_statistics.timer_count << '\n';
                std::cout << "  script_event_listeners=" << script_statistics.event_listener_count << '\n';
                std::cout << "  script_detached_roots=" << script_statistics.detached_nodes.root_count << '\n';
                std::cout << "  script_detached_nodes="
                          << script_statistics.detached_nodes.aggregate.node_count << '\n';
                std::cout << "  script_detached_max_subtree="
                          << script_statistics.detached_nodes.max_subtree_nodes << '\n';
            }
#endif
        }
    } catch (const std::exception& error) {
        std::cerr << "pseudo browser failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
