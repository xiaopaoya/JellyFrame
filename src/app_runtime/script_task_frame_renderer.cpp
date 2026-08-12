#include "app_runtime/script_task_frame_renderer.h"

#include "render_core/raster_primitives.h"

#include <algorithm>
#include <vector>

namespace jellyframe {
namespace {

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
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

ScriptTaskFrameRenderer::ScriptTaskFrameRenderer(TextPainter text_painter,
                                                 ScriptTaskFrameRendererOptions options)
    : rasterizer_(text_painter,
                  options.diagnostics,
                  SoftwareRasterizerOptions{options.max_temporary_pixels}),
      options_(options) {}

ScriptTaskFrameRenderer::ScriptTaskFrameRenderer(TextPainter text_painter,
                                                 ImagePainter image_painter,
                                                 ScriptTaskFrameRendererOptions options)
    : rasterizer_(text_painter,
                  image_painter,
                  options.diagnostics,
                  SoftwareRasterizerOptions{options.max_temporary_pixels}),
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
