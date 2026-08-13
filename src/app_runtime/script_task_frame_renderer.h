#pragma once

#include "app_runtime/script_task_frame_codec.h"
#include "render_core/software_renderer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace jellyframe {

enum class ScriptTaskFrameRenderStatus : std::uint8_t {
    Accepted,
    InvalidFrame,
    InvalidClipChain,
    FramebufferRejected,
};

struct ScriptTaskFrameRendererOptions {
    std::size_t max_clip_depth = 8;
    std::size_t max_temporary_pixels = 0;
    DiagnosticSink* diagnostics = nullptr;
    SoftwareRasterizerStatistics* rasterizer_statistics = nullptr;
    SoftwareRasterizerTiming rasterizer_timing;
};

// Read-only comparison of two value frames for retained-rendering research.
// A report never grants permission to skip paint: transparent commands and
// paint-order dependencies require a future retained contract. It only
// identifies whether frame geometry is compatible and how much command value
// churn a future bounded reuse experiment would need to handle.
struct ScriptTaskFrameDiff {
    bool viewport_equal = false;
    bool clip_chains_equal = false;
    bool display_clip_indices_equal = false;
    bool input_targets_equal = false;
    bool paint_structure_equal = false;
    std::size_t previous_command_count = 0;
    std::size_t current_command_count = 0;
    std::size_t unchanged_command_count = 0;
    std::size_t changed_command_count = 0;
    std::size_t unchanged_prefix_command_count = 0;
    std::size_t unchanged_suffix_command_count = 0;
    Rect changed_command_bounds;
    bool has_changed_command_bounds = false;
};

// Compares only value-owned frame data. `paint_structure_equal` requires an
// equal viewport plus identical clip records and per-command clip references.
// `changed_command_bounds` unions old and new bounds for changed commands; it
// is a measurement hint, not an invalidation region or a rendering shortcut.
ScriptTaskFrameDiff diff_script_task_app_frames(const ScriptTaskAppFrame& previous,
                                                const ScriptTaskAppFrame& current);

// Cumulative, value-only retained-diff telemetry. It owns at most one prior
// accepted frame and consumes the current frame only after a caller has
// completed a successful present. It never selects repaint rectangles or
// permits framebuffer/display-list reuse.
struct ScriptTaskFrameDiffStatistics {
    std::uint64_t pairs = 0;
    std::uint64_t paint_structure_equal_pairs = 0;
    std::uint64_t input_targets_equal_pairs = 0;
    std::uint64_t unchanged_commands = 0;
    std::uint64_t changed_commands = 0;
    std::uint64_t unchanged_prefix_commands = 0;
    std::uint64_t unchanged_suffix_commands = 0;
    std::uint64_t changed_bounds_pairs = 0;
    std::uint64_t changed_bounds_pixels = 0;
};

class ScriptTaskFrameDiffAccumulator final {
public:
    // Returns true only after comparing a current accepted frame to a prior
    // accepted frame. Call this after present succeeds, then do not use frame.
    bool observe_presented(ScriptTaskAppFrame&& frame);
    const ScriptTaskFrameDiffStatistics& statistics() const;
    void reset();

private:
    std::optional<ScriptTaskAppFrame> previous_;
    ScriptTaskFrameDiffStatistics statistics_;
};

// Desktop/host-side consumer for the value-only frame contract. It never
// reconstructs DOM or LayerNode state; all geometry comes from the decoded
// frame and all drawing is delegated to the platform-neutral rasterizer.
class ScriptTaskFrameRenderer final {
public:
    explicit ScriptTaskFrameRenderer(TextPainter text_painter = {},
                                     ScriptTaskFrameRendererOptions options = {});
    ScriptTaskFrameRenderer(TextPainter text_painter,
                            ImagePainter image_painter,
                            ScriptTaskFrameRendererOptions options = {});

    FrameBuffer render(const ScriptTaskAppFrame& frame,
                       Color background,
                       ScriptTaskFrameRenderStatus* status = nullptr) const;

    bool render_into(const ScriptTaskAppFrame& frame,
                     FrameBuffer& target,
                     Color background,
                     const Rect* dirty_rects = nullptr,
                     std::size_t dirty_rect_count = 0,
                     SoftwareRasterizerScratch* scratch = nullptr,
                     ScriptTaskFrameRenderStatus* status = nullptr) const;

private:
    bool collect_clip_chain(const ScriptTaskAppFrame& frame,
                            std::uint32_t clip_index,
                            std::vector<RasterClip>& output) const;
    bool validate_frame(const ScriptTaskAppFrame& frame) const;

    SoftwareRasterizer rasterizer_;
    ScriptTaskFrameRendererOptions options_;
};

} // namespace jellyframe
