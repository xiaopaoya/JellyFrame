#pragma once

#include "app_package.h"
#include "app_runtime/runtime_shaped_text_binding.h"
#include "app_runtime/script_task_command_access.h"

#if defined(_WIN32) && JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
#include "device_runtime_contracts/device_bundle.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstring>

namespace jellyframe::native {

inline constexpr bool contextual_desktop_available = true;

inline RuntimeSha256 desktop_resource_digest(std::string_view bytes) {
    if (bytes.size() > std::numeric_limits<ULONG>::max())
        throw std::runtime_error("Desktop resource SHA256 input exceeds provider limit");
    RuntimeSha256 digest{};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Desktop SHA256 provider unavailable");
    const auto status = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()),
        digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("Desktop resource SHA256 failed");
    return digest;
}

struct DesktopContextualText {
    RuntimeShapedTextBinding::Handle binding;
    contextual_text::LayoutOptions layout_options;
};

// The Desktop developer route trusts the local package chosen by the operator.
// These content digests identify its snapshot. Signed installation uses the
// independent authenticated AppInstalledBundleBinding route.
inline DesktopContextualText load_desktop_contextual_text(
    const jellyframe_example::AppPackageManifest& manifest,
    jellyframe_example::PackageResourceContext& resources) {
    if (manifest.contextual_text_pipeline.empty()) return {};
    if (manifest.script_mode != "classic")
        throw std::runtime_error("Desktop contextual textPipeline requires classic script mode");
    // Reuse the public bounded protocol parser, including its exact field/path
    // policy, with an isolated normalized summary envelope.
    const std::string summary =
        "{\"id\":\"desktop.local\",\"name\":\"Desktop\",\"role\":\"app\","
        "\"versionName\":\"1\",\"versionCode\":1,\"entry\":\"/index.html\","
        "\"minJellyFrame\":\"" + std::string(jellyframe_example::kActiveJellyFrameRuntimeVersion) +
        "\",\"minRenderCore\":\"" + std::string(jellyframe_example::kActiveRenderCoreVersion) + "\",\"script\":\"classic\","
        "\"textPipeline\":" + manifest.contextual_text_pipeline + "}";
    std::vector<std::uint8_t> envelope(kDeviceBundleHeaderBytes + summary.size());
    std::memcpy(envelope.data(), "JFAPPV0\0", 8);
    envelope[8] = static_cast<std::uint8_t>(kDeviceBundleHeaderBytes);
    envelope[10] = static_cast<std::uint8_t>(kDeviceBundleShapedFormat);
    std::memcpy(envelope.data() + kDeviceBundleHeaderBytes, summary.data(), summary.size());
    DeviceBundleDescriptor descriptor;
    descriptor.bundle_bytes = static_cast<std::uint32_t>(envelope.size());
    descriptor.summary_offset = kDeviceBundleHeaderBytes;
    descriptor.summary_bytes = static_cast<std::uint32_t>(summary.size());
    DeviceBundleMemoryReader reader(envelope.data(), envelope.size());
    DeviceBundleInspectionWorkspace workspace;
    DeviceBundleTextPipeline pipeline;
    if (read_device_bundle_text_pipeline(reader, descriptor, workspace, pipeline) != DeviceBundleStatus::Ok ||
        pipeline.kind != DeviceBundleTextPipelineKind::Jfsp2BlockV12)
        throw std::runtime_error("Desktop contextual textPipeline is invalid");

    std::array<std::string, 5> bytes;
    std::array<RuntimeSha256, 5> digests;
    constexpr std::size_t resource_budget = 16U * 1024U * 1024U;
    std::size_t total_bytes = 0;
    auto bounded_resources = resources;
    bounded_resources.max_input_bytes = resource_budget;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (!jellyframe_example::load_package_resource(pipeline.paths[index].data(), manifest.entry,
                bytes[index], &bounded_resources) || bytes[index].empty())
            throw std::runtime_error("Desktop contextual resource could not be read");
        if (bytes[index].size() > resource_budget - total_bytes)
            throw std::runtime_error("Desktop contextual resource budget exceeded");
        total_bytes += bytes[index].size();
        digests[index] = desktop_resource_digest(bytes[index]);
    }
    RuntimeShapedTextManifest selected;
    selected.font_resource = bytes[0]; selected.shaping_catalog = bytes[1];
    selected.shaping_companion = bytes[2]; selected.normalization_tables = bytes[3];
    selected.normalization_eligibility = bytes[4];
    selected.font_resource_identity = digests[0]; selected.catalog_identity = digests[1];
    selected.companion_identity = digests[2]; selected.tables_identity = digests[3];
    selected.eligibility_identity = digests[4];
    selected.source_identity = pipeline.source_identity; selected.profile_identity = pipeline.profile_identity;
    selected.shaping_glyph_count = pipeline.glyph_count; selected.resource_id = pipeline.resource_id;
    selected.raster_size = pipeline.raster_size; selected.weight = pipeline.weight; selected.selection = pipeline.selection;
    DesktopContextualText result;
    if (RuntimeShapedTextBinding::create(selected, result.binding) != RuntimeShapedTextStatus::Ready)
        throw std::runtime_error("Desktop contextual resources refused by Core");
    result.layout_options.font_family_hash = pipeline.font_family_hash;
    return result;
}

struct DesktopContextualFrame {
    RuntimeShapedTextBinding::Handle binding;
    RenderObjectPtr render_tree;
    contextual_text::LayoutResult layout;
    contextual_text::LayerCapture capture;
    LayerNodePtr input_layer;
};

inline FrameBuffer paint_desktop_contextual_frame(const DesktopContextualFrame& frame,
    int width, int height, Color background, std::size_t max_pixels,
    std::uint64_t work_limit = 32U * 1024U * 1024U) {
    if (!frame.binding || !frame.capture.valid() || width <= 0 || height <= 0 ||
        (max_pixels && std::uint64_t(width) * unsigned(height) > max_pixels))
        throw std::runtime_error("Desktop contextual paint request refused");
    FrameBuffer staged(width, height, background);
    const Rect clip{0, 0, width, height};
    const auto& commands = frame.capture.layer_tree()->display_list;
    const auto& paragraphs = frame.capture.layout().paragraphs();
    const auto& positions = frame.capture.after_command_counts();
    if (positions.size() != paragraphs.size()) throw std::runtime_error("Desktop contextual capture invalid");
    SoftwareRasterizer rasterizer;
    contextual_text::Limits limits;
    std::size_t paragraph_index = 0;
    for (std::size_t command_index = 0; command_index <= commands.size(); ++command_index) {
        while (paragraph_index < paragraphs.size() && positions[paragraph_index] == command_index) {
            const auto& paragraph = paragraphs[paragraph_index++];
            for (const auto& line : paragraph.paragraph.lines()) {
                const auto slack = std::int64_t(paragraph.content_width_q1000) - line.metrics.advance_x;
                const auto alignment = paragraph.align == TextAlign::End ? slack :
                    paragraph.align == TextAlign::Center ? slack / 2 : 0;
                const auto x = std::int64_t(paragraph.origin_x_q1000) + alignment;
                const auto y = std::int64_t(paragraph.origin_y_q1000) + line.baseline_y_q1000;
                if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX ||
                    contextual_text::paint(frame.binding->core_binding(), line.packet, staged,
                        static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), clip,
                        paragraph.color, limits, work_limit) != contextual_text::Status::ok)
                    throw std::runtime_error("Desktop contextual typed paint refused");
            }
        }
        if (command_index < commands.size()) {
            const auto& command = commands[command_index];
            // Capture admits only solid backgrounds, which need no image or clip surface.
            if (detail::command_type(command) != DisplayCommandType::FillRect)
                throw std::runtime_error("Desktop contextual background paint refused");
            rasterizer.rasterize(command, staged, clip);
        }
    }
    if (paragraph_index != paragraphs.size()) throw std::runtime_error("Desktop contextual capture order invalid");
    return staged;
}

inline DesktopContextualFrame build_desktop_contextual_frame(Node& document,
    const StyleResolver& resolver, const DesktopContextualText& selected, int width, int height,
    RenderTreeOptions render_options = {}, LayoutEngineOptions layout_options = {},
    LayerTreeBuilderOptions layer_options = {}) {
    if (!selected.binding) throw std::runtime_error("Desktop contextual binding missing");
    DesktopContextualFrame result;
    result.binding = selected.binding;
    render_options.viewport_width = width; render_options.viewport_height = height;
    result.render_tree = RenderTreeBuilder(resolver, render_options).build(document);
    if (!result.render_tree) throw std::runtime_error("Desktop contextual render tree refused");
    layout_options.text_system = nullptr;
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION)
    LayoutEngine engine(resolver, layout_options);
#else
    LayoutEngine engine(resolver, TextMeasureProvider{}, layout_options);
#endif

    auto work = selected.binding->work_limit();
    if (selected.binding->layout_document(engine, *result.render_tree, width, height,
            selected.layout_options, work, result.layout) != contextual_text::Status::ok)
        throw std::runtime_error("Desktop contextual layout refused");
    layer_options.text_system = nullptr;
    if (!LayerTreeBuilder(layer_options).build_contextual(result.layout, result.capture))
        throw std::runtime_error("Desktop contextual layer capture refused");
    // Captured root intentionally has a null box. Input borrows geometry from
    // the retained LayoutResult through this separately owned root.
    result.input_layer = LayerNodePtr(new LayerNode);
    result.input_layer->type = LayerType::Root;
    result.input_layer->reasons = LayerReasonRoot;
    result.input_layer->box = result.layout.tree();
    result.input_layer->bounds = result.layout.tree()->rect;
    result.input_layer->clip_rect = result.input_layer->bounds;
    result.input_layer->display_list = result.capture.layer_tree()->display_list;
    return result;
}
} // namespace jellyframe::native
#else
namespace jellyframe::native { inline constexpr bool contextual_desktop_available = false; }
#endif

namespace jellyframe::native {
inline jellyframe_example::AppPackage load_desktop_app_package(const std::filesystem::path& path,
    std::size_t max_input_bytes) {
    std::size_t package_limit = max_input_bytes;
    if (contextual_desktop_available && std::filesystem::is_regular_file(path)) {
        // Font/normalization assets exceed the ordinary document input limit.
        // Only the mandatory V1 container tag admits this separate bounded
        // local Desktop resource envelope. V0 retains its caller's input cap.
        std::array<std::uint8_t, jellyframe_example::kJfappHeaderSize> header{};
        std::ifstream input(path, std::ios::binary);
        input.read(reinterpret_cast<char*>(header.data()), header.size());
        if (input.gcount() == header.size() &&
            std::string_view(reinterpret_cast<const char*>(header.data()), 8) == std::string_view("JFAPPV0\0", 8) &&
            jellyframe_example::read_le16(header.data() + 8) == jellyframe_example::kJfappHeaderSize &&
            jellyframe_example::read_le16(header.data() + 10) == 1 &&
            jellyframe_example::read_le32(header.data() + 12) == 0 &&
            jellyframe_example::read_le32(header.data() + 52) == 0)
            package_limit = 20U * 1024U * 1024U;
    }
    auto package = jellyframe_example::load_app_package(path, package_limit, contextual_desktop_available);
    if (package.manifest.contextual_text_pipeline.empty() && package.bundle_bytes.size() > max_input_bytes)
        throw std::runtime_error("Desktop scalar package exceeds input budget");
    return package;
}
} // namespace jellyframe::native
