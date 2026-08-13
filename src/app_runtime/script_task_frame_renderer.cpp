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

std::uint16_t display_clip_index_at(const ScriptTaskAppFrame& frame, std::size_t index) {
    return frame.display_clip_indices.empty() ? kScriptTaskNoClip : frame.display_clip_indices[index];
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

} // namespace

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

    const std::size_t comparable_count = std::min(report.previous_command_count, report.current_command_count);
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
        if (has_previous && has_current && equal_command_at(previous, index, current, index)) {
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
      options_(options) {}

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
      options_(options) {}

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

} // namespace jellyframe
