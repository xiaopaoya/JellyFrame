#pragma once
#include "app_runtime/script_task_canonical_codec.h"
#include "app_runtime/script_task_frame_renderer.h"

namespace jellyframe::detail::canonical {
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API
class GroupTree {
public:
    explicit GroupTree(const ScriptTaskAppFrame& f) : nodes_(f.groups.size()) {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            auto& n = nodes_[i]; const auto& g = f.groups[i];
            n.type = g.type; n.bounds = g.bounds; n.clip_rect = g.clip_rect; n.clip_border_radius = g.clip_border_radius;
            set_layer_radius_y(n, g.clip_border_radius_y);
            n.has_clip = g.has_clip; n.opacity = g.opacity; n.transform = g.transform; n.has_transform = g.has_transform;
            n.transform_origin_x_percent = g.origin_x_percent; n.transform_origin_y_percent = g.origin_y_percent;
            n.reasons = g.reasons; n.scroll_y = g.scroll_y; n.max_scroll_y = g.max_scroll_y;
            n.z_index = g.z_index; n.source_order = static_cast<std::size_t>(g.source_order);
            n.display_list.assign(f.display_list.begin() + g.command_begin,
                f.display_list.begin() + g.command_begin + g.command_count);
            if (i) nodes_[g.parent].children.emplace_back(&n, LayerNodeDeleter{true});
        }
    }
    const LayerNode& root() const { return nodes_.front(); }
private:
    std::vector<LayerNode> nodes_;
};

inline bool same_group(const ScriptTaskFrameGroup& a, const ScriptTaskFrameGroup& b) {
    const auto same_rect = [](Rect x, Rect y) { return x.x == y.x && x.y == y.y && x.width == y.width && x.height == y.height; };
    return a.parent == b.parent && a.command_begin == b.command_begin && a.command_count == b.command_count &&
        a.type == b.type && same_rect(a.bounds, b.bounds) && same_rect(a.clip_rect, b.clip_rect) &&
        a.clip_border_radius == b.clip_border_radius && a.clip_border_radius_y == b.clip_border_radius_y &&
        a.has_clip == b.has_clip && a.opacity == b.opacity &&
        a.transform.translate_x == b.transform.translate_x && a.transform.translate_y == b.transform.translate_y &&
        a.transform.scale_x == b.transform.scale_x && a.transform.scale_y == b.transform.scale_y &&
        a.transform.rotate_degrees == b.transform.rotate_degrees && a.has_transform == b.has_transform &&
        a.origin_x_percent == b.origin_x_percent && a.origin_y_percent == b.origin_y_percent && a.reasons == b.reasons &&
        a.scroll_y == b.scroll_y && a.max_scroll_y == b.max_scroll_y && a.z_index == b.z_index && a.source_order == b.source_order;
}

inline bool render(const ScriptTaskAppFrame& frame, FrameBuffer& target, Color background,
                   const Rect* dirty, std::size_t dirty_count, TextPainter text, ImagePainter image,
                   const ScriptTaskFrameRendererOptions& options, ScriptTaskFrameRenderStatus* status) {
    const auto fail = [&](ScriptTaskFrameRenderStatus why) { if (status) *status = why; return false; };
    if (target.width != frame.viewport.width || target.height != frame.viewport.height ||
        frame.viewport.x != 0 || frame.viewport.y != 0 || frame.viewport.width <= 0 || frame.viewport.height <= 0)
        return fail(ScriptTaskFrameRenderStatus::InvalidFrame);
    if (!script_task_frame_uses_image_provider(frame, options.image_provider)) return fail(ScriptTaskFrameRenderStatus::InvalidFrame);
    if (frame.image_provider_id) image = options.image_provider->painter();
    ScriptTaskAppFrameCodecOptions limits;
    limits.version = elliptical_available ? 10 : 9; limits.max_commands = limits.max_text_bytes = limits.max_input_targets = limits.max_payload_bytes =
        std::numeric_limits<std::size_t>::max();
    limits.max_groups = options.max_groups; limits.max_group_depth = options.max_group_depth;
    limits.max_clips = 65535; limits.max_clip_depth = options.max_clip_depth;
    std::size_t total;
    try {
        if (validate(frame, limits, total) != Status::Accepted) return fail(ScriptTaskFrameRenderStatus::InvalidFrame);
    } catch (const std::bad_alloc&) { return fail(ScriptTaskFrameRenderStatus::FramebufferRejected); }
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    for (const auto& c : frame.display_list) {
        const auto k = command_kind(c);
        // Cropped Core deliberately falls gradients back to a solid color and
        // omits shadows. Such fallback is not successful v9 semantic replay.
        if ((k >= 2 && k <= 5) || k == 9) return fail(ScriptTaskFrameRenderStatus::InvalidFrame);
    }
#endif
    bool prepared = false;
    for (const auto& c : frame.display_list) prepared = prepared || c.type() == DisplayCommandType::PreparedText;
    if (prepared && (!options.text_catalog || !script_task_frame_uses_catalog(frame, *options.text_catalog)))
        return fail(ScriptTaskFrameRenderStatus::InvalidFrame);
    const auto pixels = static_cast<std::uint64_t>(target.width) * static_cast<std::uint64_t>(target.height);
    if (pixels > std::numeric_limits<std::size_t>::max() || target.pixels.size() != pixels)
        return fail(ScriptTaskFrameRenderStatus::FramebufferRejected);
    // Transactional rendering owns one staging framebuffer, charged before
    // Core's aggregate live offscreen budget. Zero remains explicitly unlimited.
    if (options.max_temporary_pixels && pixels >= options.max_temporary_pixels)
        return fail(ScriptTaskFrameRenderStatus::FramebufferRejected);
    class Sink final : public DiagnosticSink {
    public:
        explicit Sink(DiagnosticSink* next) : next_(next) {}
        void report(DiagnosticStage stage, DiagnosticSeverity severity, std::string_view code,
                    std::string_view message, std::string_view detail) override {
            if (stage == DiagnosticStage::Paint && severity != DiagnosticSeverity::Info) refused = true;
            if (next_) next_->report(stage, severity, code, message, detail);
        }
        bool refused = false;
    private: DiagnosticSink* next_;
    } sink(options.diagnostics);
    try {
        GroupTree tree(frame);
        FrameBuffer staged = target;
        SoftwareCompositor::Options core;
        core.max_offscreen_pixels = options.max_temporary_pixels ? options.max_temporary_pixels - static_cast<std::size_t>(pixels) : 0;
        core.diagnostics = &sink; core.rasterizer_timing = options.rasterizer_timing;
        core.text_system = options.text_catalog ? &options.text_catalog->text_system() : nullptr;
        core.text_limits = options.text_limits;
        SoftwareCompositor compositor(text, image, core);
        compositor.render_into(tree.root(), staged, background, dirty, dirty_count);
        if (sink.refused) return fail(ScriptTaskFrameRenderStatus::InvalidFrame);
        target = std::move(staged);
        if (status) *status = ScriptTaskFrameRenderStatus::Accepted;
        return true;
    } catch (const std::bad_alloc&) { return fail(ScriptTaskFrameRenderStatus::FramebufferRejected); }
}
#endif
} // namespace jellyframe::detail::canonical
