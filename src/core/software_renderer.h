#pragma once

#include "core/geometry.h"
#include "core/host.h"
#include "core/layer_tree.h"

#include <cstddef>
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

class SoftwareRasterizer {
public:
    explicit SoftwareRasterizer(TextPainter text_painter = {});

    void rasterize(const DisplayList& display_list, FrameBuffer& target, Rect clip, int offset_x = 0, int offset_y = 0) const;
    void rasterize(const DisplayCommand& command, FrameBuffer& target, Rect clip, int offset_x = 0, int offset_y = 0) const;

private:
    TextPainter text_painter_;
};

class SoftwareCompositor {
public:
    struct Options {
        std::size_t max_offscreen_pixels = 0;
    };

    explicit SoftwareCompositor(TextPainter text_painter = {}, Options options = {});

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
