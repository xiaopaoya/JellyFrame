#pragma once

#include "render_core/diagnostics.h"
#include "render_core/geometry.h"
#include "render_core/host.h"
#include "render_core/layer_tree.h"

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

using TextPaintCallback = bool (*)(FrameBuffer& target,
                                   Rect rect,
                                   Color color,
                                   const std::string& text,
                                   int font_size,
                                   int font_weight,
                                   TextCommandAlign align,
                                   bool single_line,
                                   void* context);

struct TextPainter {
    TextPaintCallback paint = nullptr;
    void* context = nullptr;
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
};

class SoftwareRasterizer {
public:
    explicit SoftwareRasterizer(TextPainter text_painter = {}, DiagnosticSink* diagnostics = nullptr);
    SoftwareRasterizer(TextPainter text_painter, ImagePainter image_painter, DiagnosticSink* diagnostics = nullptr);

    void rasterize(const DisplayList& display_list, FrameBuffer& target, Rect clip, int offset_x = 0, int offset_y = 0) const;
    void rasterize(const DisplayCommand& command, FrameBuffer& target, Rect clip, int offset_x = 0, int offset_y = 0) const;

private:
    TextPainter text_painter_;
    ImagePainter image_painter_;
    DiagnosticSink* diagnostics_ = nullptr;
};

class SoftwareCompositor {
public:
    struct Options {
        std::size_t max_framebuffer_pixels = 0;
        std::size_t max_offscreen_pixels = 0;
        DiagnosticSink* diagnostics = nullptr;
        bool smooth_scaled_layers = true;
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
                     std::size_t dirty_rect_count) const;

private:
    SoftwareRasterizer rasterizer_;
    Options options_;

    void composite_layer(const LayerNode& layer,
                         FrameBuffer& target,
                         Rect clip,
                         int offset_x,
                         int offset_y,
                         float inherited_opacity = 1.0F) const;
};

void write_ppm(const FrameBuffer& frame_buffer, const std::string& path);
void write_bmp(const FrameBuffer& frame_buffer, const std::string& path);
void write_image(const FrameBuffer& frame_buffer, const std::string& path);
std::size_t count_non_background_pixels(const FrameBuffer& frame_buffer, Color background);
HostFrameBufferView frame_buffer_view(const FrameBuffer& frame_buffer);
bool present_frame(const FrameBuffer& frame_buffer,
                   const HostFrameSink& frame_sink,
                   const Rect* dirty_rects = nullptr,
                   std::size_t dirty_rect_count = 0);

} // namespace jellyframe
