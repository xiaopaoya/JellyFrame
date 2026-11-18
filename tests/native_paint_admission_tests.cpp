#include "../tools/native/native_budgets.h"
#include "../tools/native/native_paint.h"
#if __has_include("render_core/fractional_sampling.h")
#include "render_core/fractional_sampling.h"
#define JELLYFRAME_TEST_FRACTIONAL_PAINT 1
#endif
#include <cassert>
#include <iostream>

int main() {
    using namespace jellyframe;
    HostBudgets embedded;
    assert(embedded.max_layers == 1024);
    auto desktop = embedded;
    native::admit_desktop_core_layers(desktop);
    assert(desktop.max_layers == desktop.max_layout_boxes * 4 + 1);
    assert(embedded.max_layers == 1024);
    desktop.max_layout_boxes = std::numeric_limits<std::size_t>::max();
    bool overflow = false;
    try { native::admit_desktop_core_layers(desktop); }
    catch (const std::overflow_error&) { overflow = true; }
    assert(overflow);

    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = {0, 0, 8, 8};
    const Color background{255, 255, 255, 255};
#if JELLYFRAME_TEST_FRACTIONAL_PAINT
    DisplayCommand command;
    command.type = DisplayCommandType::Image;
    command.image_handle = 7;
    command.rect = command.sampling_rect = {0, 0, 8, 8};
    assert(encode_fractional_sampling({{0, 1}, {0, 1}, {8, 1}, {8, 1}}, command.text));
    root.display_list.push_back(command);
    SoftwareCompositor missing;
    bool refused = false;
    try { auto frame = native::paint_frame(missing, root, 8, 8, background, 64); (void)frame; }
    catch (const std::runtime_error& error) {
        refused = std::string(error.what()).find("Core paint refused") != std::string::npos;
    }
    assert(refused); // Nonempty background-only output is not an accepted frame.
    ImagePainter painter;
    painter.paint_fractional_clipped = [](FrameBuffer& target, const ImagePaintFractionalRequest& request, void*) {
        if (request.image_handle != 7) return false;
        for (int y = request.clip.y; y < request.clip.y + request.clip.height; ++y)
            for (int x = request.clip.x; x < request.clip.x + request.clip.width; ++x)
                target.pixel(x, y) = {21, 43, 65, 255};
        return true;
    };
    SoftwareCompositor ready({}, painter);
    const auto frame = native::paint_frame(ready, root, 8, 8, background, 64);
    assert(frame.pixel(4, 4).r == 21);
#else
    SoftwareCompositor ready;
    const auto frame = native::paint_frame(ready, root, 8, 8, background, 64);
    assert(frame.pixels.size() == 64 && frame.pixel(4, 4).r == 255);
#endif
    bool budget_refused = false;
    try { auto oversized = native::paint_frame(ready, root, 8, 8, background, 63); (void)oversized; }
    catch (const std::runtime_error&) { budget_refused = true; }
    assert(budget_refused);
    std::cout << "Native paint, desktop reservation and pixel budget passed\n";
}
