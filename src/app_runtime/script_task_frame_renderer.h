#pragma once

#include "app_runtime/script_task_frame_codec.h"
#include "render_core/software_renderer.h"

#include <array>
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

// Returns the complete destination-space area that the current value-frame
// rasterizer may write for one command. It is intentionally conservative:
// callers must fall back when it returns false rather than shrink a dirty
// region. In particular, a BoxShadow record is usable only because its
// renderer is contractually clipped to its already-expanded command rect.
bool script_task_frame_command_visual_bounds(const DisplayCommand& command, Rect& output);

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

    // Applies this renderer's host painter contracts to conservative command
    // bounds. Retained experiments must call this instead of assuming rect.
    bool command_visual_bounds(const DisplayCommand& command, Rect& output) const;

private:
    bool collect_clip_chain(const ScriptTaskAppFrame& frame,
                            std::uint32_t clip_index,
                            std::vector<RasterClip>& output) const;
    bool validate_frame(const ScriptTaskAppFrame& frame) const;

    SoftwareRasterizer rasterizer_;
    ScriptTaskFrameRendererOptions options_;
    TextPainter text_painter_;
    ImagePainter image_painter_;
};

// Explicit, desktop-oriented retained-diff correctness probe. It copies the
// last successfully presented value frame/image only when observe_presented()
// is called by its owner. While enabled, every replay candidate is compared
// against a canonical full-frame render before it can be reported as replayed.
// It is deliberately not a default rendering path or a port framebuffer API.
enum class ScriptTaskFrameRetainedReplayStatus : std::uint8_t {
    FullFrameDisabled,
    FullFrameNoPrevious,
    FullFrameIneligible,
    Replayed,
    PixelMismatchFallback,
    RenderRejected,
};

// Why an enabled probe returned the canonical full-frame path instead of a
// candidate. `None` is also used for disabled/no-prior-frame paths, which have
// their own counters and are not replay-eligibility failures.
enum class ScriptTaskFrameRetainedReplayFallbackReason : std::uint8_t {
    None,
    InvalidFrame,
    RetainedBudget,
    ResourceGeneration,
    Background,
    PreviousImageDimensions,
    PaintSkeleton,
    NoChangedCommands,
    UnsupportedVisualBounds,
    ReplayRegionBudget,
    NoIntersectingCommandGroups,
    CandidateRenderRejected,
    CanonicalRenderRejected,
    Count,
};

constexpr std::size_t kScriptTaskFrameRetainedReplayFallbackReasonCount =
    static_cast<std::size_t>(ScriptTaskFrameRetainedReplayFallbackReason::Count);

struct ScriptTaskFrameRetainedReplayOptions {
    bool enabled = false;
    // Zero means that retention/replay is not budgeted and therefore falls
    // back. The caller must opt in with an explicit per-image bound. The
    // probe owns one previous image and one independent candidate image.
    std::size_t max_retained_pixels = 0;
    std::size_t max_replay_pixels = 0;
};

struct ScriptTaskFrameRetainedReplayStatistics {
    std::uint64_t full_frame_disabled = 0;
    std::uint64_t full_frame_no_previous = 0;
    std::uint64_t full_frame_ineligible = 0;
    std::uint64_t full_frame_rejected = 0;
    std::uint64_t candidates = 0;
    std::uint64_t replays = 0;
    std::uint64_t pixel_mismatch_fallbacks = 0;
    std::uint64_t pixel_mismatch_pixels = 0;
    // Indexed by ScriptTaskFrameRetainedReplayFallbackReason. `None` is
    // intentionally zero because disabled/no-prior cases have dedicated
    // counters above.
    std::array<std::uint64_t, kScriptTaskFrameRetainedReplayFallbackReasonCount> ineligible_by_reason{};
    std::uint64_t candidate_region_pixels = 0;
    std::uint64_t replayed_command_groups = 0;
    // Commands dispatched to the rasterizer. A clip group is dispatched as a
    // unit, so this can exceed the count whose bounds intersect the region.
    std::uint64_t replayed_commands = 0;
    std::uint64_t retained_image_pixels = 0;
    std::uint64_t candidate_image_pixels = 0;
    // First exact-RGBA mismatch observed since construction/reset. These are
    // diagnostic-only; the full-frame result is always returned on mismatch.
    bool has_first_pixel_mismatch = false;
    int first_pixel_mismatch_x = -1;
    int first_pixel_mismatch_y = -1;
};

class ScriptTaskFrameRetainedReplay final {
public:
    explicit ScriptTaskFrameRetainedReplay(ScriptTaskFrameRetainedReplayOptions options = {});

    // Renders a full frame or an independently owned replay candidate. Every
    // candidate is checked against a canonical full-frame RGBA render. A
    // successful return does not retain anything: call observe_presented()
    // only after the caller's normal present operation succeeds.
    bool render_into(const ScriptTaskFrameRenderer& renderer,
                     const ScriptTaskAppFrame& frame,
                     FrameBuffer& target,
                     Color background,
                     std::uint64_t resource_generation,
                     ScriptTaskFrameRetainedReplayStatus* status = nullptr,
                     ScriptTaskFrameRetainedReplayFallbackReason* fallback_reason = nullptr) const;

    // Transfers only value-owned state after a successful present. The image
    // is copied into bounded probe storage so the caller may reuse target.
    bool observe_presented(const ScriptTaskAppFrame& frame,
                           const FrameBuffer& image,
                           Color background,
                           std::uint64_t resource_generation);
    const ScriptTaskFrameRetainedReplayStatistics& statistics() const;
    void reset();

private:
    bool within_retained_budget(const ScriptTaskAppFrame& frame) const;
    bool eligible(const ScriptTaskFrameRenderer& renderer,
                  const ScriptTaskAppFrame& frame,
                  Color background,
                  std::uint64_t resource_generation,
                  Rect& changed_region,
                  std::size_t& replayed_command_groups,
                  std::size_t& replayed_commands,
                  ScriptTaskFrameRetainedReplayFallbackReason& fallback_reason) const;

    ScriptTaskFrameRetainedReplayOptions options_;
    mutable ScriptTaskFrameRetainedReplayStatistics statistics_;
    std::optional<ScriptTaskAppFrame> previous_frame_;
    FrameBuffer previous_image_;
    mutable FrameBuffer candidate_image_;
    Color previous_background_;
    std::uint64_t previous_resource_generation_ = 0;
};

} // namespace jellyframe
