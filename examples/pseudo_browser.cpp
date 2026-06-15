#include "core/css_parser.h"
#include "core/document_script.h"
#include "core/document_style.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"
#include "core/style.h"

#if defined(WEARWEB_ENABLE_SCRIPTING)
#include "script/jerryscript_runtime.h"
#endif

#include "example_css_io.h"

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

using namespace wearweb;

namespace {

constexpr std::size_t kMaxInputBytes = 512 * 1024;

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

std::size_t count_dom_nodes(const Node& node) {
    std::size_t count = 1;
    for (const auto& child : node.children) {
        count += count_dom_nodes(*child);
    }
    return count;
}

std::size_t count_render_objects(const RenderObject& object) {
    std::size_t count = 1;
    for (const auto& child : object.children) {
        count += count_render_objects(*child);
    }
    return count;
}

std::size_t count_layout_boxes(const LayoutBox& box) {
    std::size_t count = 1;
    for (const auto& child : box.children) {
        count += count_layout_boxes(*child);
    }
    return count;
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
    int viewport_width = 360;
    int viewport_height = 240;
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

BrowserOptions parse_options(int argc, char** argv) {
    if (argc < 4) {
        throw std::runtime_error(
            "usage: wearweb_pseudo_browser page.html style.css output.ppm [viewport_width] [viewport_height] "
            "[--script script.js]");
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
            width_set = true;
            continue;
        }
        if (!height_set) {
            options.viewport_height = parse_int_arg(argv[i], options.viewport_height);
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
        const std::string html = read_file_limited(options.html_path);

        HtmlParser html_parser;
        CssParser css_parser;
        auto document = html_parser.parse(html);

        bool script_ran = false;
        bool script_ok = false;
        std::string script_output;
        std::size_t timer_callbacks = 0;
        std::size_t document_script_count = 0;
#if defined(WEARWEB_ENABLE_SCRIPTING)
        std::unique_ptr<JerryScriptRuntime> runtime;
#endif
#if defined(WEARWEB_ENABLE_SCRIPTING)
        wearweb_example::ScriptLoadContext script_context;
        const std::filesystem::path html_path(options.html_path);
        script_context.base_dir = html_path.has_parent_path() ? html_path.parent_path() : std::filesystem::current_path();
        script_context.max_input_bytes = kMaxInputBytes;
        std::vector<DocumentScript> document_scripts =
            collect_classic_scripts(*document, wearweb_example::load_linked_script, &script_context);
        document_script_count = document_scripts.size();
#endif
        if (!options.script_path.empty()
#if defined(WEARWEB_ENABLE_SCRIPTING)
            || !document_scripts.empty()
#endif
        ) {
#if defined(WEARWEB_ENABLE_SCRIPTING)
            runtime = std::make_unique<JerryScriptRuntime>();
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
            if (script_ok && !options.script_path.empty()) {
                const ScriptEvaluationResult script_result =
                    runtime->eval(read_file_limited(options.script_path), options.script_path);
                script_ok = script_result.ok;
                script_output = script_result.ok ? script_result.value : script_result.error;
            }
            script_ran = true;
            for (int now_ms = 16; now_ms <= options.pump_timers_ms; now_ms += 16) {
                timer_callbacks += runtime->pump_timers(static_cast<std::uint64_t>(now_ms), 32);
            }
            if (options.pump_timers_ms > 0 && options.pump_timers_ms % 16 != 0) {
                timer_callbacks += runtime->pump_timers(static_cast<std::uint64_t>(options.pump_timers_ms), 32);
            }
#else
            throw std::runtime_error("this build was compiled without WEARWEB_BUILD_SCRIPTING=ON");
#endif
        }

        Stylesheet stylesheet = css_parser.parse(
            wearweb_example::read_author_css_for_document(options.css_path, *document, kMaxInputBytes));
        StyleResolver resolver(std::move(stylesheet));

        RenderTreeBuilder render_tree_builder(resolver);
        auto render_tree = render_tree_builder.build(*document);
        LayoutEngine layout_engine(resolver);
        auto layout_tree = layout_engine.layout(*render_tree, options.viewport_width);
        LayerTreeBuilder layer_tree_builder;
        auto layer_tree = layer_tree_builder.build(*layout_tree);
        DisplayList display_list = layer_tree_builder.flatten(*layer_tree);

        SoftwareCompositor compositor;
        const Color background = page_background_color(*document, resolver);
        FrameBuffer frame_buffer = compositor.render(
            *layer_tree, options.viewport_width, options.viewport_height, background);
        ImageFrameSinkContext frame_sink_context{options.output_path, false};
        const Rect full_dirty{0, 0, frame_buffer.width, frame_buffer.height};
        const HostFrameSink frame_sink{write_image_frame_sink, &frame_sink_context};
        if (!present_frame(frame_buffer, frame_sink, &full_dirty, 1)) {
            throw std::runtime_error("failed to present output frame");
        }

        std::cout << "WearWeb pseudo browser\n";
        std::cout << "  output=" << options.output_path << '\n';
        std::cout << "  viewport=" << options.viewport_width << "x" << options.viewport_height << '\n';
        std::cout << "  dom_nodes=" << count_dom_nodes(*document) << '\n';
        std::cout << "  render_objects=" << count_render_objects(*render_tree) << '\n';
        std::cout << "  layout_boxes=" << count_layout_boxes(*layout_tree) << '\n';
        std::cout << "  layers=" << count_layers(*layer_tree) << '\n';
        std::cout << "  display_commands=" << display_list.size() << '\n';
        std::cout << "  frame_sink=" << (frame_sink_context.ok ? "image" : "none") << '\n';
        std::cout << "  non_background_pixels="
                  << count_non_background_pixels(frame_buffer, background) << '\n';
        if (script_ran) {
            std::cout << "  script=" << options.script_path << '\n';
            std::cout << "  document_scripts=" << document_script_count << '\n';
            std::cout << "  script_ok=" << (script_ok ? "true" : "false") << '\n';
            std::cout << "  script_result=" << script_output << '\n';
            std::cout << "  timer_callbacks=" << timer_callbacks << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "pseudo browser failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
