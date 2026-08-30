#pragma once

#include "render_core/diagnostics.h"
#include "render_core/geometry.h"
#include "render_core/host.h"
#include "render_core/layer_tree.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jellyframe {

struct FrameBuffer {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;

    FrameBuffer() = default;
    FrameBuffer(int width, int height, Color clear_color);

    void resize(int new_width, int new_height, Color clear_color);
    void clear(Color clear_color);
    bool contains(int x, int y) const;
    Color& pixel(int x, int y);
    const Color& pixel(int x, int y) const;
};

// Optional caller-owned storage for clipped text and image commands.
struct SoftwareRasterizerScratch {
    FrameBuffer temporary_surface;
    // Used by value-frame consumers when a flattened command carries an
    // affine transform. It is separate from temporary_surface because text
    // and rounded-clip raster paths may use that surface recursively.
    FrameBuffer transformed_surface;

    void release();
};

// Optional caller-owned counters for diagnosing rounded value-frame clips.
// They are intentionally plain counters: rasterizer instances are task-local.
struct SoftwareRasterizerStatistics {
    std::size_t rounded_clip_runs = 0;
    std::size_t rounded_clip_commands = 0;
    // Indexed by DisplayCommandType. Counts commands replayed into a rounded
    // temporary surface, excluding rectangular and non-clipped draws.
    std::array<std::size_t, kDisplayCommandTypeCount> rounded_clip_replayed_commands_by_type{};
    // Indexed by DisplayCommandType. Each value is the sum of command-rect
    // intersections with the rounded temporary surface. Values may overlap
    // across commands and therefore are candidate raster areas, not visible
    // or opaque pixel counts.
    std::array<std::size_t, kDisplayCommandTypeCount> rounded_clip_replay_candidate_pixels_by_type{};
    // Indexed by DisplayCommandType. Accumulates caller-clock microseconds
    // spent replaying commands into rounded temporary surfaces. The value is
    // populated only when SoftwareRasterizerOptions::timing is supplied.
    std::array<std::uint64_t, kDisplayCommandTypeCount> rounded_clip_replay_microseconds_by_type{};
    std::uint64_t rounded_clip_replay_microseconds = 0;
    // Optional caller-clock time spent preparing (including clearing) the
    // rounded temporary surface and compositing it back to the target. These
    // exclude command replay, which remains attributed by command type above.
    std::uint64_t rounded_clip_surface_prepare_microseconds = 0;
    std::uint64_t rounded_clip_composite_microseconds = 0;
    // Optional caller-clock attribution for the two rounded-composite row
    // shapes. When timing is enabled their sum equals the composite total,
    // except for invalid clock samples or counter saturation.
    std::uint64_t rounded_clip_full_coverage_composite_microseconds = 0;
    std::uint64_t rounded_clip_coverage_sampled_composite_microseconds = 0;
    std::size_t rounded_clip_replay_timing_invalid_samples = 0;
    std::size_t rounded_clip_mask_pixels = 0;
    std::size_t rounded_clip_temporary_pixels = 0;
    std::size_t rounded_clip_rectangular_fast_paths = 0;
    std::size_t rounded_clip_opaque_direct_pixels = 0;
    std::size_t rounded_clip_blended_pixels = 0;
    std::size_t rounded_clip_full_coverage_pixels = 0;
    std::size_t rounded_clip_coverage_sampled_pixels = 0;
    // Rounded composite row-shape counters. They explain whether a
    // full-coverage output is one opaque span or fragmented by alpha.
    std::size_t rounded_clip_full_coverage_rows = 0;
    std::size_t rounded_clip_full_coverage_opaque_rows = 0;
    std::size_t rounded_clip_full_coverage_opaque_runs = 0;
    std::size_t rounded_clip_coverage_sampled_rows = 0;
    // Work-shape counters for pixels in rounded-corner rows. A clip
    // evaluation is one clip-chain visit; a math evaluation is a visit that
    // enters the 4x4 subpixel circle test rather than returning rectangular
    // coverage immediately. The output counters classify final chain
    // coverage, after every applicable clip has been combined.
    std::size_t rounded_clip_coverage_clip_evaluations = 0;
    std::size_t rounded_clip_coverage_math_evaluations = 0;
    // Pixels inside a conservative row span where every clip is known to
    // return 255 without a coverage query. The companion counter is the
    // number of clip visits avoided by that span, not a paint-pixel count.
    std::size_t rounded_clip_coverage_known_full_pixels = 0;
    std::size_t rounded_clip_coverage_known_full_clip_bypasses = 0;
    std::size_t rounded_clip_coverage_zero_pixels = 0;
    std::size_t rounded_clip_coverage_partial_pixels = 0;
    std::size_t rounded_clip_coverage_full_pixels = 0;
    std::size_t rounded_clip_budget_rejections = 0;
    std::size_t rounded_clip_allocation_rejections = 0;

    void reset();
};

// A value-only clip record consumed by the rasterizer. The caller supplies
// records from outermost to innermost order; rectangular clips stay on the
// direct fast path and rounded clips use a bounded temporary surface.
struct RasterClip {
    Rect rect;
    int border_radius = 0;
};

using TextPaintCallback = bool (*)(FrameBuffer& target,
                                   Rect rect,
                                   Color color,
                                   const std::string& text,
                                   int font_size,
                                   int font_weight,
                                   TextCommandAlign align,
                                   bool single_line,
                                   void* context);

using TextPaintFamilyCallback = bool (*)(FrameBuffer& target,
                                         Rect rect,
                                         Color color,
                                         const std::string& text,
                                         int font_size,
                                         int font_weight,
                                         std::uint32_t font_family_hash,
                                         TextCommandAlign align,
                                         bool single_line,
                                         void* context);

struct TextPainter {
    TextPaintCallback paint = nullptr;
    void* context = nullptr;
    TextPaintFamilyCallback paint_family = nullptr;
    // True only when every successful callback invocation writes within the
    // supplied rect. Normal paint does not require this; retained/diff
    // consumers use it as an explicit visual-bounds contract.
    bool writes_only_within_rect = false;
};

using ImagePaintCallback = bool (*)(FrameBuffer& target,
                                    Rect rect,
                                    std::uint32_t image_handle,
                                    ObjectFit object_fit,
                                    ObjectPosition object_position,
                                    ImageRendering image_rendering,
                                    void* context);

struct ImagePainter {
    ImagePaintCallback paint = nullptr;
    void* context = nullptr;
    // Same opt-in contract as TextPainter. Samplers that can bleed beyond the
    // supplied rect must leave this false.
    bool writes_only_within_rect = false;
};

// A port supplies a monotonic microsecond clock only for profiling. The
// rasterizer has no platform clock dependency and never calls it by default.
using SoftwareRasterizerNowMicrosecondsCallback = std::uint64_t (*)(void* context);

struct SoftwareRasterizerTiming {
    SoftwareRasterizerNowMicrosecondsCallback now_microseconds = nullptr;
    void* context = nullptr;
};

struct SoftwareRasterizerOptions {
    // Applies only to clipped text/image temporary surfaces. Zero is unlimited.
    std::size_t max_temporary_pixels = 0;
    // When supplied, records bounded rounded-clip work without allocating.
    SoftwareRasterizerStatistics* statistics = nullptr;
    // Optional timing for rounded temporary-surface preparation, command
    // replay, and rounded coverage composition. No clock is read by default.
    SoftwareRasterizerTiming timing;
};

class SoftwareRasterizer {
public:
    explicit SoftwareRasterizer(TextPainter text_painter = {},
                                DiagnosticSink* diagnostics = nullptr,
                                SoftwareRasterizerOptions options = {});
    SoftwareRasterizer(TextPainter text_painter,
                       ImagePainter image_painter,
                       DiagnosticSink* diagnostics = nullptr,
                       SoftwareRasterizerOptions options = {});

    void rasterize(const DisplayList& display_list, FrameBuffer& target, Rect clip, int offset_x = 0, int offset_y = 0) const;
    void rasterize(const DisplayList& display_list,
                   FrameBuffer& target,
                   Rect clip,
                   int offset_x,
                   int offset_y,
                   SoftwareRasterizerScratch* scratch) const;
    void rasterize(const DisplayCommand& command,
                   FrameBuffer& target,
                   Rect clip,
                   int offset_x = 0,
                   int offset_y = 0,
                   SoftwareRasterizerScratch* scratch = nullptr) const;
    void rasterize_clipped(const DisplayCommand& command,
                           FrameBuffer& target,
                           Rect clip,
                           int offset_x,
                           int offset_y,
                           const RasterClip* clips,
                           std::size_t clip_count,
                           SoftwareRasterizerScratch* scratch = nullptr) const;
    // Consecutive commands with one common clip chain can share a temporary
    // surface. This preserves paint order while avoiding one rounded-mask pass
    // per command on embedded software renderers.
    void rasterize_clipped(const DisplayCommand* commands,
                           std::size_t command_count,
                           FrameBuffer& target,
                           Rect clip,
                           int offset_x,
                           int offset_y,
                           const RasterClip* clips,
                           std::size_t clip_count,
                           SoftwareRasterizerScratch* scratch = nullptr) const;

private:
    TextPainter text_painter_;
    ImagePainter image_painter_;
    DiagnosticSink* diagnostics_ = nullptr;
    SoftwareRasterizerOptions options_;

    bool prepare_temporary_surface(FrameBuffer& surface, int width, int height) const;
};

class SoftwareCompositor {
public:
    struct Options {
        // Zero means unlimited. A layer that needs an offscreen surface for a
        // transform or rounded clip is skipped when this aggregate live-pixel
        // budget cannot be met; opacity-only layers may use direct fallback.
        std::size_t max_framebuffer_pixels = 0;
        std::size_t max_offscreen_pixels = 0;
        DiagnosticSink* diagnostics = nullptr;
        bool smooth_scaled_layers = true;
    };

    struct Scratch {
        SoftwareRasterizerScratch rasterizer;

        // Retained across frames so nested compositing bounds do not allocate
        // on every repaint. Entries are addressed by index during recursion.
        struct CompositeBoundsEntry {
            Rect source_bounds;
            Rect visual_bounds;
            std::vector<std::size_t> children;
        };

        std::vector<CompositeBoundsEntry> composite_bounds;
        std::size_t active_composite_bounds = 0;

        void release();
    };

    SoftwareCompositor();
    explicit SoftwareCompositor(TextPainter text_painter);
    SoftwareCompositor(TextPainter text_painter, Options options);
    SoftwareCompositor(TextPainter text_painter, ImagePainter image_painter);
    SoftwareCompositor(TextPainter text_painter, ImagePainter image_painter, Options options);

    FrameBuffer render(const LayerNode& root, int viewport_width, int viewport_height, Color background) const;
    void render_into(const LayerNode& root, FrameBuffer& target, Color background) const;
    void render_into(const LayerNode& root,
                     FrameBuffer& target,
                     Color background,
                     const Rect* dirty_rects,
                     std::size_t dirty_rect_count,
                     Scratch* scratch = nullptr) const;

private:
    SoftwareRasterizer rasterizer_;
    Options options_;

    void composite_layer(const LayerNode& layer,
                         const Scratch::CompositeBoundsEntry& bounds,
                         FrameBuffer& target,
                         Rect clip,
                         int offset_x,
                         int offset_y,
                         float inherited_opacity = 1.0F,
                         std::size_t active_offscreen_pixels = 0,
                         SoftwareRasterizerScratch* scratch = nullptr,
                         const Scratch* compositor_scratch = nullptr) const;
};

#ifdef JELLYFRAME_ENABLE_IMAGE_FILE_IO
void write_ppm(const FrameBuffer& frame_buffer, const std::string& path);
void write_bmp(const FrameBuffer& frame_buffer, const std::string& path);
void write_image(const FrameBuffer& frame_buffer, const std::string& path);
#endif
std::size_t count_non_background_pixels(const FrameBuffer& frame_buffer, Color background);
HostFrameBufferView frame_buffer_view(const FrameBuffer& frame_buffer);
bool present_frame(const FrameBuffer& frame_buffer,
                   const HostFrameSink& frame_sink,
                   const Rect* dirty_rects = nullptr,
                   std::size_t dirty_rect_count = 0);

} // namespace jellyframe
