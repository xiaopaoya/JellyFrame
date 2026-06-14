#pragma once

#include "core/geometry.h"
#include "core/layer_tree.h"

#include <cstddef>
#include <string>
#include <vector>

namespace wearweb {

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

class SoftwareRasterizer {
public:
    void rasterize(const DisplayList& display_list, FrameBuffer& target, Rect clip, int offset_x = 0, int offset_y = 0) const;
    void rasterize(const DisplayCommand& command, FrameBuffer& target, Rect clip, int offset_x = 0, int offset_y = 0) const;
};

class SoftwareCompositor {
public:
    FrameBuffer render(const LayerNode& root, int viewport_width, int viewport_height, Color background) const;

private:
    SoftwareRasterizer rasterizer_;

    void composite_layer(const LayerNode& layer, FrameBuffer& target, Rect clip, int offset_x, int offset_y) const;
};

void write_ppm(const FrameBuffer& frame_buffer, const std::string& path);
void write_bmp(const FrameBuffer& frame_buffer, const std::string& path);
void write_image(const FrameBuffer& frame_buffer, const std::string& path);
std::size_t count_non_background_pixels(const FrameBuffer& frame_buffer, Color background);

} // namespace wearweb
