#pragma once

#include "app_runtime/script_task_frame_codec.h"
#include "render_core/software_renderer.h"

#include <cstddef>
#include <cstdint>
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
