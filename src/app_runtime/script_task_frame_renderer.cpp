#include "app_runtime/script_task_frame_renderer.h"

#include "render_core/raster_primitives.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace jellyframe {
namespace {

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
}

template <typename Value>
void add_saturating(Value& target, Value value) {
    if (value > std::numeric_limits<Value>::max() - target) {
        target = std::numeric_limits<Value>::max();
        return;
    }
    target += value;
}

void add_replay_fallback_reason(ScriptTaskFrameRetainedReplayStatistics& statistics,
                                ScriptTaskFrameRetainedReplayFallbackReason reason) {
    const std::size_t index = static_cast<std::size_t>(reason);
    if (reason == ScriptTaskFrameRetainedReplayFallbackReason::None ||
        index >= statistics.ineligible_by_reason.size()) {
        return;
    }
    add_saturating(statistics.ineligible_by_reason[index], std::uint64_t{1});
}

Rect intersect_rect(Rect left, Rect right) {
    const int x1 = std::max(left.x, right.x);
    const int y1 = std::max(left.y, right.y);
    const int x2 = std::min(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::min(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    if (x2 <= x1 || y2 <= y1) {
        return {x1, y1, 0, 0};
    }
    return {x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

Rect union_rect(Rect left, Rect right) {
    if (empty_rect(left)) return right;
    if (empty_rect(right)) return left;
    const int x1 = std::min(left.x, right.x);
    const int y1 = std::min(left.y, right.y);
    const int x2 = std::max(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::max(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    return {x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

Rect expand_rect(Rect rect, int amount) {
    if (empty_rect(rect) || amount <= 0) return rect;
    const int x1 = safe_edge(rect.x, -amount);
    const int y1 = safe_edge(rect.y, -amount);
    const int x2 = safe_edge(safe_edge(rect.x, rect.width), amount);
    const int y2 = safe_edge(safe_edge(rect.y, rect.height), amount);
    return {x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

bool contains_rect(Rect outer, Rect inner) {
    return !empty_rect(inner) &&
        inner.x >= outer.x && inner.y >= outer.y &&
        safe_edge(inner.x, inner.width) <= safe_edge(outer.x, outer.width) &&
        safe_edge(inner.y, inner.height) <= safe_edge(outer.y, outer.height);
}

bool equal_rect(Rect left, Rect right) {
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
}

bool equal_color(Color left, Color right) {
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

struct FrameBufferComparison {
    bool equal = false;
    std::uint64_t mismatch_pixels = 0;
    int first_mismatch_x = -1;
    int first_mismatch_y = -1;
};

FrameBufferComparison compare_framebuffer(const FrameBuffer& left, const FrameBuffer& right) {
    FrameBufferComparison comparison;
    if (left.width != right.width || left.height != right.height || left.pixels.size() != right.pixels.size()) {
        comparison.mismatch_pixels = std::numeric_limits<std::uint64_t>::max();
        return comparison;
    }
    for (std::size_t index = 0; index < left.pixels.size(); ++index) {
        if (equal_color(left.pixels[index], right.pixels[index])) {
            continue;
        }
        add_saturating(comparison.mismatch_pixels, std::uint64_t{1});
        if (comparison.first_mismatch_x < 0) {
            comparison.first_mismatch_x = static_cast<int>(index % static_cast<std::size_t>(left.width));
            comparison.first_mismatch_y = static_cast<int>(index / static_cast<std::size_t>(left.width));
        }
    }
    comparison.equal = comparison.mismatch_pixels == 0;
    return comparison;
}

bool equal_display_command(const DisplayCommand& left, const DisplayCommand& right) {
    return left.type == right.type && equal_rect(left.rect, right.rect) && equal_color(left.color, right.color) &&
        equal_color(left.color2, right.color2) && left.text == right.text &&
        left.border_radius == right.border_radius && left.stroke_width == right.stroke_width &&
        left.font_size == right.font_size && left.font_weight == right.font_weight &&
        left.font_family_hash == right.font_family_hash && left.text_align == right.text_align &&
        left.text_single_line == right.text_single_line && left.gradient_axis == right.gradient_axis &&
        left.gradient_stop_percent == right.gradient_stop_percent && left.image_handle == right.image_handle &&
        left.object_fit == right.object_fit && left.object_position.x_percent == right.object_position.x_percent &&
        left.object_position.y_percent == right.object_position.y_percent &&
        left.image_rendering == right.image_rendering;
}

bool equal_clip(const ScriptTaskFrameClip& left, const ScriptTaskFrameClip& right) {
    return equal_rect(left.rect, right.rect) && left.border_radius == right.border_radius &&
        left.parent_clip == right.parent_clip;
}

bool equal_input_target(const ScriptTaskInputTarget& left, const ScriptTaskInputTarget& right) {
    return left.target_key == right.target_key && equal_rect(left.rect, right.rect) && left.enabled == right.enabled &&
        left.clip_index == right.clip_index;
}

template <typename Value, typename Equal>
bool equal_vector(const std::vector<Value>& left, const std::vector<Value>& right, Equal equal) {
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin(), right.end(), equal);
}

bool has_valid_clip_parallelism(const ScriptTaskAppFrame& frame) {
    return frame.display_clip_indices.empty() || frame.display_clip_indices.size() == frame.display_list.size();
}

bool has_valid_value_frame_shape(const ScriptTaskAppFrame& frame) {
    return frame.viewport.width > 0 && frame.viewport.height > 0 && has_valid_clip_parallelism(frame);
}

std::uint16_t display_clip_index_at(const ScriptTaskAppFrame& frame, std::size_t index) {
    return frame.display_clip_indices.empty() ? kScriptTaskNoClip : frame.display_clip_indices[index];
}

// A partial replay of a rounded clip group may otherwise re-composite a
// translucent edge against a locally rebuilt underlay. Treat the effective
// rounded clip as an atomic invalidation unit for the desktop correctness
// probe. This deliberately does not alter the public dirty-render contract.
bool rounded_clip_chain_bounds(const ScriptTaskAppFrame& frame,
                               std::uint16_t clip_index,
                               Rect viewport,
                               Rect& bounds) {
    bounds = viewport;
    if (clip_index == kScriptTaskNoClip) {
        return false;
    }

    bool has_rounded_clip = false;
    std::uint32_t current = clip_index;
    std::size_t depth = 0;
    while (current != kScriptTaskNoParentClip) {
        if (current >= frame.clips.size() || depth++ >= frame.clips.size()) {
            return false;
        }
        const ScriptTaskFrameClip& clip = frame.clips[current];
        bounds = intersect_rect(bounds, clip.rect);
        if (empty_rect(bounds)) {
            return false;
        }
        has_rounded_clip = has_rounded_clip || has_corner_radius(clip.border_radius);
        current = clip.parent_clip;
    }
    return has_rounded_clip;
}

bool equal_command_at(const ScriptTaskAppFrame& previous,
                      std::size_t previous_index,
                      const ScriptTaskAppFrame& current,
                      std::size_t current_index) {
    return equal_display_command(previous.display_list[previous_index], current.display_list[current_index]) &&
        display_clip_index_at(previous, previous_index) == display_clip_index_at(current, current_index);
}

std::uint64_t clipped_rect_pixels(Rect rect, Rect viewport) {
    const Rect visible = intersect_rect(rect, viewport);
    if (empty_rect(visible)) {
        return 0;
    }
    const std::uint64_t width = static_cast<std::uint64_t>(visible.width);
    const std::uint64_t height = static_cast<std::uint64_t>(visible.height);
    if (width > std::numeric_limits<std::uint64_t>::max() / height) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return width * height;
}

std::uint64_t framebuffer_bytes(const FrameBuffer& framebuffer) {
    const std::size_t pixel_count = framebuffer.pixels.size();
    if (pixel_count > std::numeric_limits<std::uint64_t>::max() / sizeof(Color)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(pixel_count) * sizeof(Color);
}

std::vector<Rect> normalize_dirty_rects(const Rect* dirty_rects,
                                        std::size_t dirty_rect_count,
                                        Rect target) {
    std::vector<Rect> normalized;
    normalized.reserve(dirty_rect_count);
    for (std::size_t index = 0; index < dirty_rect_count; ++index) {
        const Rect dirty = intersect_rect(dirty_rects[index], target);
        if (empty_rect(dirty)) continue;
        if (std::any_of(normalized.begin(), normalized.end(),
                        [dirty](Rect existing) { return contains_rect(existing, dirty); })) {
            continue;
        }
        normalized.erase(std::remove_if(normalized.begin(), normalized.end(),
                                        [dirty](Rect existing) { return contains_rect(dirty, existing); }),
                         normalized.end());
        normalized.push_back(dirty);
    }
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t left = 0; left + 1 < normalized.size() && !merged; ++left) {
            for (std::size_t right = left + 1; right < normalized.size(); ++right) {
                if (empty_rect(intersect_rect(normalized[left], normalized[right]))) continue;
                normalized[left] = union_rect(normalized[left], normalized[right]);
                normalized.erase(normalized.begin() + static_cast<std::ptrdiff_t>(right));
                merged = true;
                break;
            }
        }
    }
    return normalized;
}

void clear_rect(FrameBuffer& target, Rect rect, Color color) {
    const Rect visible = intersect_rect(rect, {0, 0, target.width, target.height});
    if (empty_rect(visible)) return;
    for (int y = visible.y; y < safe_edge(visible.y, visible.height); ++y) {
        for (int x = visible.x; x < safe_edge(visible.x, visible.width); ++x) {
            target.pixel(x, y) = color;
        }
    }
}

bool valid_display_command_type(DisplayCommandType type) {
    return static_cast<std::size_t>(type) < kDisplayCommandTypeCount;
}

bool equal_paint_skeleton(const ScriptTaskAppFrame& previous, const ScriptTaskAppFrame& current) {
    const ScriptTaskFrameDiff report = diff_script_task_app_frames(previous, current);
    if (!report.paint_structure_equal || previous.display_list.size() != current.display_list.size()) {
        return false;
    }
    for (std::size_t index = 0; index < previous.display_list.size(); ++index) {
        if (previous.display_list[index].type != current.display_list[index].type) {
            return false;
        }
    }
    return true;
}

} // namespace

bool script_task_frame_command_visual_bounds(const DisplayCommand& command, Rect& output) {
    if (!valid_display_command_type(command.type) || command.rect.width <= 0 || command.rect.height <= 0) {
        output = {};
        return false;
    }
    // Text and image painters are host callbacks without a published bounded
    // write contract. Their glyph/sampling footprint therefore cannot yet be
    // used to shrink a replay region. The first probe falls back instead of
    // relying on rect, even though the built-in fallback happens to stay in it.
    if (command.type == DisplayCommandType::Text || command.type == DisplayCommandType::Image) {
        output = {};
        return false;
    }
    // Every remaining current SoftwareRasterizer command is clipped to rect
    // before it can write. LayerTree expands BoxShadow rects before publication.
    output = command.rect;
    return true;
}

ScriptTaskFrameDiff diff_script_task_app_frames(const ScriptTaskAppFrame& previous,
                                                const ScriptTaskAppFrame& current) {
    ScriptTaskFrameDiff report;
    report.viewport_equal = equal_rect(previous.viewport, current.viewport);
    report.previous_command_count = previous.display_list.size();
    report.current_command_count = current.display_list.size();
    report.clip_chains_equal = equal_vector(previous.clips, current.clips, equal_clip);
    report.input_targets_equal = equal_vector(previous.input_targets, current.input_targets, equal_input_target);
    const bool valid_parallelism = has_valid_clip_parallelism(previous) && has_valid_clip_parallelism(current);
    report.display_clip_indices_equal = valid_parallelism &&
        report.previous_command_count == report.current_command_count;
    if (report.display_clip_indices_equal) {
        for (std::size_t index = 0; index < report.previous_command_count; ++index) {
            if (display_clip_index_at(previous, index) != display_clip_index_at(current, index)) {
                report.display_clip_indices_equal = false;
                break;
            }
        }
    }
    report.paint_structure_equal = report.viewport_equal && report.clip_chains_equal &&
        report.display_clip_indices_equal;

    const std::size_t comparable_count = valid_parallelism
        ? std::min(report.previous_command_count, report.current_command_count)
        : 0;
    for (std::size_t index = 0; index < comparable_count; ++index) {
        if (equal_command_at(previous, index, current, index)) {
            ++report.unchanged_command_count;
        }
    }
    report.changed_command_count = std::max(report.previous_command_count, report.current_command_count) -
        report.unchanged_command_count;
    while (report.unchanged_prefix_command_count < comparable_count &&
           equal_command_at(previous,
                            report.unchanged_prefix_command_count,
                            current,
                            report.unchanged_prefix_command_count)) {
        ++report.unchanged_prefix_command_count;
    }
    while (report.unchanged_suffix_command_count + report.unchanged_prefix_command_count < comparable_count) {
        const std::size_t previous_index = report.previous_command_count - 1 - report.unchanged_suffix_command_count;
        const std::size_t current_index = report.current_command_count - 1 - report.unchanged_suffix_command_count;
        if (!equal_command_at(previous, previous_index, current, current_index)) {
            break;
        }
        ++report.unchanged_suffix_command_count;
    }
    const std::size_t maximum_count = std::max(report.previous_command_count, report.current_command_count);
    for (std::size_t index = 0; index < maximum_count; ++index) {
        const bool has_previous = index < report.previous_command_count;
        const bool has_current = index < report.current_command_count;
        if (valid_parallelism && has_previous && has_current && equal_command_at(previous, index, current, index)) {
            continue;
        }
        if (has_previous) {
            report.changed_command_bounds = report.has_changed_command_bounds
                ? union_rect(report.changed_command_bounds, previous.display_list[index].rect)
                : previous.display_list[index].rect;
            report.has_changed_command_bounds = true;
        }
        if (has_current) {
            report.changed_command_bounds = report.has_changed_command_bounds
                ? union_rect(report.changed_command_bounds, current.display_list[index].rect)
                : current.display_list[index].rect;
            report.has_changed_command_bounds = true;
        }
    }
    return report;
}

bool ScriptTaskFrameDiffAccumulator::observe_presented(ScriptTaskAppFrame&& frame) {
    if (!previous_.has_value()) {
        previous_.emplace(std::move(frame));
        return false;
    }
    const ScriptTaskFrameDiff report = diff_script_task_app_frames(*previous_, frame);
    add_saturating(statistics_.pairs, std::uint64_t{1});
    if (report.paint_structure_equal) {
        add_saturating(statistics_.paint_structure_equal_pairs, std::uint64_t{1});
    }
    if (report.input_targets_equal) {
        add_saturating(statistics_.input_targets_equal_pairs, std::uint64_t{1});
    }
    add_saturating(statistics_.unchanged_commands, static_cast<std::uint64_t>(report.unchanged_command_count));
    add_saturating(statistics_.changed_commands, static_cast<std::uint64_t>(report.changed_command_count));
    add_saturating(statistics_.unchanged_prefix_commands,
                   static_cast<std::uint64_t>(report.unchanged_prefix_command_count));
    add_saturating(statistics_.unchanged_suffix_commands,
                   static_cast<std::uint64_t>(report.unchanged_suffix_command_count));
    if (report.has_changed_command_bounds) {
        add_saturating(statistics_.changed_bounds_pairs, std::uint64_t{1});
        add_saturating(statistics_.changed_bounds_pixels,
                       clipped_rect_pixels(report.changed_command_bounds, frame.viewport));
    }
    *previous_ = std::move(frame);
    return true;
}

const ScriptTaskFrameDiffStatistics& ScriptTaskFrameDiffAccumulator::statistics() const {
    return statistics_;
}

void ScriptTaskFrameDiffAccumulator::reset() {
    previous_.reset();
    statistics_ = {};
}

ScriptTaskFrameRenderer::ScriptTaskFrameRenderer(TextPainter text_painter,
                                                 ScriptTaskFrameRendererOptions options)
    : rasterizer_(text_painter,
                  options.diagnostics,
                  SoftwareRasterizerOptions{
                      options.max_temporary_pixels,
                      options.rasterizer_statistics,
                      options.rasterizer_timing}),
      options_(options),
      text_painter_(text_painter) {}

ScriptTaskFrameRenderer::ScriptTaskFrameRenderer(TextPainter text_painter,
                                                 ImagePainter image_painter,
                                                 ScriptTaskFrameRendererOptions options)
    : rasterizer_(text_painter,
                  image_painter,
                  options.diagnostics,
                  SoftwareRasterizerOptions{
                      options.max_temporary_pixels,
                      options.rasterizer_statistics,
                      options.rasterizer_timing}),
      options_(options),
      text_painter_(text_painter),
      image_painter_(image_painter) {}

bool ScriptTaskFrameRenderer::command_visual_bounds(const DisplayCommand& command, Rect& output) const {
    if (command.type == DisplayCommandType::Text) {
        if (!text_painter_.writes_only_within_rect || command.rect.width <= 0 || command.rect.height <= 0) {
            output = {};
            return false;
        }
        output = command.rect;
        return true;
    }
    if (command.type == DisplayCommandType::Image) {
        if (!image_painter_.writes_only_within_rect || command.rect.width <= 0 || command.rect.height <= 0) {
            output = {};
            return false;
        }
        output = command.rect;
        return true;
    }
    return script_task_frame_command_visual_bounds(command, output);
}

FrameBuffer ScriptTaskFrameRenderer::render(const ScriptTaskAppFrame& frame,
                                            Color background,
                                            ScriptTaskFrameRenderStatus* status) const {
    FrameBuffer target(frame.viewport.width, frame.viewport.height, background);
    if (target.width != frame.viewport.width || target.height != frame.viewport.height) {
        if (status != nullptr) *status = ScriptTaskFrameRenderStatus::FramebufferRejected;
        return {};
    }
    if (!render_into(frame, target, background, nullptr, 0, nullptr, status)) {
        return {};
    }
    return target;
}

bool ScriptTaskFrameRenderer::validate_frame(const ScriptTaskAppFrame& frame) const {
    if (frame.viewport.width <= 0 || frame.viewport.height <= 0) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::Paint,
                          DiagnosticSeverity::Warning,
                          "script-frame-viewport",
                          "Value frame viewport is invalid",
                          "non-positive dimensions");
        return false;
    }
    if (!frame.display_clip_indices.empty() &&
        frame.display_clip_indices.size() != frame.display_list.size()) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::Paint,
                          DiagnosticSeverity::Warning,
                          "script-frame-clip-parallelism",
                          "Value frame clip references are not parallel to the display list",
                          "display_clip_indices size mismatch");
        return false;
    }
    return true;
}

bool ScriptTaskFrameRenderer::collect_clip_chain(const ScriptTaskAppFrame& frame,
                                                 std::uint32_t clip_index,
                                                 std::vector<RasterClip>& output) const {
    output.clear();
    if (clip_index == kScriptTaskNoClip) return true;

    std::vector<std::uint32_t> reversed;
    reversed.reserve(std::min(options_.max_clip_depth, frame.clips.size()));
    std::uint32_t current = clip_index;
    while (current != kScriptTaskNoParentClip) {
        if (current >= frame.clips.size() || reversed.size() >= options_.max_clip_depth ||
            std::find(reversed.begin(), reversed.end(), current) != reversed.end()) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Paint,
                              DiagnosticSeverity::Warning,
                              "script-frame-clip-chain",
                              "Value frame clip chain is invalid or exceeds its depth budget",
                              "index, cycle or depth");
            return false;
        }
        reversed.push_back(current);
        current = frame.clips[current].parent_clip;
    }
    output.reserve(reversed.size());
    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
        const ScriptTaskFrameClip& clip = frame.clips[*it];
        if (clip.rect.width <= 0 || clip.rect.height <= 0) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Paint,
                              DiagnosticSeverity::Warning,
                              "script-frame-clip-geometry",
                              "Value frame clip has an empty geometry",
                              "clip skipped");
            return false;
        }
        output.push_back({clip.rect, clip.border_radius});
    }
    return true;
}

bool ScriptTaskFrameRenderer::render_into(const ScriptTaskAppFrame& frame,
                                          FrameBuffer& target,
                                          Color background,
                                          const Rect* dirty_rects,
                                          std::size_t dirty_rect_count,
                                          SoftwareRasterizerScratch* scratch,
                                          ScriptTaskFrameRenderStatus* status) const {
    if (status != nullptr) *status = ScriptTaskFrameRenderStatus::Accepted;
    if (!validate_frame(frame) || target.width != frame.viewport.width ||
        target.height != frame.viewport.height) {
        if (status != nullptr) {
            *status = target.width == frame.viewport.width && target.height == frame.viewport.height
                ? ScriptTaskFrameRenderStatus::InvalidFrame
                : ScriptTaskFrameRenderStatus::FramebufferRejected;
        }
        return false;
    }

    const Rect target_rect{0, 0, target.width, target.height};
    std::vector<Rect> repaint;
    if (dirty_rects == nullptr || dirty_rect_count == 0) {
        repaint.push_back(target_rect);
    } else {
        repaint = normalize_dirty_rects(dirty_rects, dirty_rect_count, target_rect);
    }
    std::vector<RasterClip> clip_chain;
    for (const Rect dirty : repaint) {
        clear_rect(target, dirty, background);
        std::size_t command_begin = 0;
        while (command_begin < frame.display_list.size()) {
            const std::uint32_t clip_index = frame.display_clip_indices.empty()
                ? kScriptTaskNoClip
                : frame.display_clip_indices[command_begin];
            std::size_t command_end = command_begin + 1;
            while (command_end < frame.display_list.size() &&
                   (frame.display_clip_indices.empty() ||
                    frame.display_clip_indices[command_end] == clip_index)) {
                ++command_end;
            }
            if (!collect_clip_chain(frame, clip_index, clip_chain)) {
                if (status != nullptr) *status = ScriptTaskFrameRenderStatus::InvalidClipChain;
                return false;
            }
            rasterizer_.rasterize_clipped(frame.display_list.data() + command_begin,
                                          command_end - command_begin,
                                          target,
                                          dirty,
                                          0,
                                          0,
                                          clip_chain.empty() ? nullptr : clip_chain.data(),
                                          clip_chain.size(),
                                          scratch);
            command_begin = command_end;
        }
    }
    return true;
}

ScriptTaskFrameRetainedReplay::ScriptTaskFrameRetainedReplay(ScriptTaskFrameRetainedReplayOptions options)
    : options_(options) {}

bool ScriptTaskFrameRetainedReplay::within_retained_budget(const ScriptTaskAppFrame& frame) const {
    std::size_t pixels = 0;
    return options_.max_retained_pixels != 0 && frame.viewport.width > 0 && frame.viewport.height > 0 &&
        checked_multiply(static_cast<std::size_t>(frame.viewport.width),
                         static_cast<std::size_t>(frame.viewport.height),
                         pixels) &&
        pixels <= options_.max_retained_pixels;
}

bool ScriptTaskFrameRetainedReplay::eligible(const ScriptTaskFrameRenderer& renderer,
                                             const ScriptTaskAppFrame& frame,
                                             Color background,
                                             std::uint64_t resource_generation,
                                             Rect& changed_region,
                                             std::size_t& replayed_command_groups,
                                             std::size_t& replayed_commands,
                                             ScriptTaskFrameRetainedReplayFallbackReason& fallback_reason) const {
    changed_region = {};
    replayed_command_groups = 0;
    replayed_commands = 0;
    fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::None;
    if (!previous_frame_.has_value()) {
        return false;
    }
    if (!within_retained_budget(frame)) {
        fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::RetainedBudget;
        return false;
    }
    if (previous_resource_generation_ != resource_generation) {
        fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::ResourceGeneration;
        return false;
    }
    if (!equal_color(previous_background_, background)) {
        fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::Background;
        return false;
    }
    if (previous_image_.width != frame.viewport.width || previous_image_.height != frame.viewport.height) {
        fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::PreviousImageDimensions;
        return false;
    }
    if (!equal_paint_skeleton(*previous_frame_, frame)) {
        fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::PaintSkeleton;
        return false;
    }

    const ScriptTaskFrameDiff report = diff_script_task_app_frames(*previous_frame_, frame);
    if (report.changed_command_count == 0 || !report.has_changed_command_bounds) {
        fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::NoChangedCommands;
        return false;
    }
    const Rect viewport{0, 0, frame.viewport.width, frame.viewport.height};
    for (std::size_t index = 0; index < frame.display_list.size(); ++index) {
        if (equal_command_at(*previous_frame_, index, frame, index)) {
            continue;
        }
        Rect old_bounds;
        Rect new_bounds;
        if (!renderer.command_visual_bounds(previous_frame_->display_list[index], old_bounds) ||
            !renderer.command_visual_bounds(frame.display_list[index], new_bounds)) {
            fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::UnsupportedVisualBounds;
            return false;
        }
        changed_region = empty_rect(changed_region) ? union_rect(old_bounds, new_bounds)
                                                     : union_rect(changed_region, union_rect(old_bounds, new_bounds));
    }

    // A rounded group that overlaps a changed underlay must also be replayed
    // as a whole. It is not enough to inspect only changed commands: a local
    // un-clipped command beneath a rounded translucent group has the same
    // partial-composite hazard. The union can expose another rounded group, so
    // converge across the finite display-list group set before budgeting.
    for (std::size_t pass = 0; pass < frame.display_list.size(); ++pass) {
        bool expanded = false;
        std::size_t begin = 0;
        while (begin < frame.display_list.size()) {
            const std::uint16_t clip_index = display_clip_index_at(frame, begin);
            std::size_t end = begin + 1;
            bool intersects = false;
            while (end < frame.display_list.size() && display_clip_index_at(frame, end) == clip_index) {
                ++end;
            }
            for (std::size_t index = begin; index < end; ++index) {
                Rect bounds;
                if (!renderer.command_visual_bounds(frame.display_list[index], bounds)) {
                    fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::UnsupportedVisualBounds;
                    return false;
                }
                if (!empty_rect(intersect_rect(bounds, changed_region))) {
                    intersects = true;
                }
            }
            Rect rounded_clip_bounds;
            if (intersects && rounded_clip_chain_bounds(frame, clip_index, viewport, rounded_clip_bounds)) {
                const Rect expanded_region = union_rect(changed_region, rounded_clip_bounds);
                expanded = expanded || !equal_rect(expanded_region, changed_region);
                changed_region = expanded_region;
            }
            begin = end;
        }
        if (!expanded) break;
    }
    // SoftwareRasterizer's rounded coverage may sample immediately outside a
    // command's integer rect. Replay must repaint that one-pixel fringe too,
    // otherwise a nested rounded clip can retain stale edge coverage.
    changed_region = intersect_rect(expand_rect(changed_region, 1), viewport);
    const std::uint64_t pixels = clipped_rect_pixels(changed_region, viewport);
    if (pixels == 0 || options_.max_replay_pixels == 0 || pixels > options_.max_replay_pixels) {
        fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::ReplayRegionBudget;
        return false;
    }
    std::size_t begin = 0;
    while (begin < frame.display_list.size()) {
        const std::uint16_t clip_index = display_clip_index_at(frame, begin);
        std::size_t end = begin + 1;
        bool intersects = false;
        while (end < frame.display_list.size() && display_clip_index_at(frame, end) == clip_index) {
            ++end;
        }
        for (std::size_t index = begin; index < end; ++index) {
            Rect bounds;
            if (!renderer.command_visual_bounds(frame.display_list[index], bounds)) {
                fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::UnsupportedVisualBounds;
                return false;
            }
            if (!empty_rect(intersect_rect(bounds, changed_region))) {
                intersects = true;
            }
        }
        if (intersects) {
            ++replayed_command_groups;
            // render_into() dispatches whole adjacent clip groups to preserve
            // their paint ordering, even when only some members intersect.
            replayed_commands += end - begin;
        }
        begin = end;
    }
    if (replayed_command_groups == 0) {
        fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::NoIntersectingCommandGroups;
        return false;
    }
    return true;
}

bool ScriptTaskFrameRetainedReplay::render_into(const ScriptTaskFrameRenderer& renderer,
                                                const ScriptTaskAppFrame& frame,
                                                FrameBuffer& target,
                                                Color background,
                                                std::uint64_t resource_generation,
                                                ScriptTaskFrameRetainedReplayStatus* status,
                                                ScriptTaskFrameRetainedReplayFallbackReason* fallback_reason) const {
    if (fallback_reason != nullptr) *fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::None;
    const auto full_frame = [&](ScriptTaskFrameRetainedReplayStatus result) {
        if (!has_valid_value_frame_shape(frame)) {
            add_saturating(statistics_.full_frame_rejected, std::uint64_t{1});
            if (status != nullptr) *status = ScriptTaskFrameRetainedReplayStatus::RenderRejected;
            return false;
        }
        target = FrameBuffer(frame.viewport.width, frame.viewport.height, background);
        statistics_.canonical_output_bytes = framebuffer_bytes(target);
        const bool rendered = renderer.render_into(frame, target, background);
        if (!rendered) {
            add_saturating(statistics_.full_frame_rejected, std::uint64_t{1});
            if (status != nullptr) *status = ScriptTaskFrameRetainedReplayStatus::RenderRejected;
            return false;
        }
        if (result == ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled) {
            add_saturating(statistics_.full_frame_disabled, std::uint64_t{1});
        } else if (result == ScriptTaskFrameRetainedReplayStatus::FullFrameNoPrevious) {
            add_saturating(statistics_.full_frame_no_previous, std::uint64_t{1});
        } else {
            add_saturating(statistics_.full_frame_ineligible, std::uint64_t{1});
        }
        if (status != nullptr) *status = result;
        return true;
    };

    // Do not let a malformed current frame enter the diff/replay analysis.
    // The canonical renderer then remains the only validator of clip records.
    if (!has_valid_value_frame_shape(frame)) {
        add_replay_fallback_reason(statistics_, ScriptTaskFrameRetainedReplayFallbackReason::InvalidFrame);
        if (fallback_reason != nullptr) {
            *fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::InvalidFrame;
        }
        return full_frame(ScriptTaskFrameRetainedReplayStatus::FullFrameIneligible);
    }

    if (!options_.enabled) {
        return full_frame(ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled);
    }
    if (!previous_frame_.has_value()) {
        return full_frame(ScriptTaskFrameRetainedReplayStatus::FullFrameNoPrevious);
    }

    Rect changed_region;
    std::size_t replayed_groups = 0;
    std::size_t replayed_commands = 0;
    ScriptTaskFrameRetainedReplayFallbackReason eligibility_reason =
        ScriptTaskFrameRetainedReplayFallbackReason::None;
    if (!eligible(renderer,
                  frame,
                  background,
                  resource_generation,
                  changed_region,
                  replayed_groups,
                  replayed_commands,
                  eligibility_reason)) {
        add_replay_fallback_reason(statistics_, eligibility_reason);
        if (fallback_reason != nullptr) *fallback_reason = eligibility_reason;
        return full_frame(ScriptTaskFrameRetainedReplayStatus::FullFrameIneligible);
    }

    candidate_image_ = previous_image_;
    statistics_.candidate_image_pixels = candidate_image_.pixels.size();
    statistics_.candidate_image_bytes = framebuffer_bytes(candidate_image_);
    if (!renderer.render_into(frame, candidate_image_, background, &changed_region, 1)) {
        add_replay_fallback_reason(statistics_, ScriptTaskFrameRetainedReplayFallbackReason::CandidateRenderRejected);
        if (fallback_reason != nullptr) {
            *fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::CandidateRenderRejected;
        }
        return full_frame(ScriptTaskFrameRetainedReplayStatus::FullFrameIneligible);
    }
    add_saturating(statistics_.candidates, std::uint64_t{1});
    add_saturating(statistics_.candidate_region_pixels,
                   clipped_rect_pixels(changed_region, Rect{0, 0, frame.viewport.width, frame.viewport.height}));
    add_saturating(statistics_.replayed_command_groups, static_cast<std::uint64_t>(replayed_groups));
    add_saturating(statistics_.replayed_commands, static_cast<std::uint64_t>(replayed_commands));

    target = FrameBuffer(frame.viewport.width, frame.viewport.height, background);
    statistics_.canonical_output_bytes = framebuffer_bytes(target);
    ScriptTaskFrameRenderStatus full_status = ScriptTaskFrameRenderStatus::InvalidFrame;
    if (!renderer.render_into(frame, target, background, nullptr, 0, nullptr, &full_status) ||
        full_status != ScriptTaskFrameRenderStatus::Accepted) {
        add_replay_fallback_reason(statistics_, ScriptTaskFrameRetainedReplayFallbackReason::CanonicalRenderRejected);
        if (fallback_reason != nullptr) {
            *fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::CanonicalRenderRejected;
        }
        add_saturating(statistics_.full_frame_rejected, std::uint64_t{1});
        if (status != nullptr) *status = ScriptTaskFrameRetainedReplayStatus::RenderRejected;
        return false;
    }
    const FrameBufferComparison comparison = compare_framebuffer(candidate_image_, target);
    if (!comparison.equal) {
        add_saturating(statistics_.pixel_mismatch_fallbacks, std::uint64_t{1});
        add_saturating(statistics_.pixel_mismatch_pixels, comparison.mismatch_pixels);
        if (!statistics_.has_first_pixel_mismatch) {
            statistics_.has_first_pixel_mismatch = true;
            statistics_.first_pixel_mismatch_x = comparison.first_mismatch_x;
            statistics_.first_pixel_mismatch_y = comparison.first_mismatch_y;
        }
        if (status != nullptr) *status = ScriptTaskFrameRetainedReplayStatus::PixelMismatchFallback;
        return true;
    }
    target = candidate_image_;
    add_saturating(statistics_.replays, std::uint64_t{1});
    if (status != nullptr) *status = ScriptTaskFrameRetainedReplayStatus::Replayed;
    return true;
}

bool ScriptTaskFrameRetainedReplay::observe_presented(const ScriptTaskAppFrame& frame,
                                                      const FrameBuffer& image,
                                                      Color background,
                                                      std::uint64_t resource_generation) {
    std::size_t pixels = 0;
    if (!options_.enabled || !has_valid_value_frame_shape(frame) || !within_retained_budget(frame) ||
        image.width != frame.viewport.width ||
        image.height != frame.viewport.height ||
        !checked_multiply(static_cast<std::size_t>(frame.viewport.width),
                          static_cast<std::size_t>(frame.viewport.height),
                          pixels) || image.pixels.size() != pixels) {
        return false;
    }
    previous_frame_ = frame;
    previous_image_ = image;
    previous_background_ = background;
    previous_resource_generation_ = resource_generation;
    statistics_.retained_image_pixels = pixels;
    statistics_.retained_image_bytes = framebuffer_bytes(previous_image_);
    return true;
}

const ScriptTaskFrameRetainedReplayStatistics& ScriptTaskFrameRetainedReplay::statistics() const {
    return statistics_;
}

void ScriptTaskFrameRetainedReplay::reset() {
    previous_frame_.reset();
    previous_image_ = {};
    candidate_image_ = {};
    previous_background_ = {};
    previous_resource_generation_ = 0;
    statistics_ = {};
}

} // namespace jellyframe
