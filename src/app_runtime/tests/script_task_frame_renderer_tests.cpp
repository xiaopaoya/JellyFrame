#include "app_runtime/script_task_frame_renderer.h"

#include <cassert>
#include <iostream>

using namespace jellyframe;

namespace {

ScriptTaskAppFrame rounded_frame() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 40, 40};
    DisplayCommand fill;
    fill.type = DisplayCommandType::FillRect;
    fill.rect = {0, 0, 40, 40};
    fill.color = {20, 120, 240, 255};
    frame.display_list.push_back(fill);
    frame.clips = {
        {{8, 8, 24, 24}, 8, kScriptTaskNoParentClip},
        {{12, 12, 16, 16}, 5, 0},
    };
    frame.display_clip_indices = {1};
    return frame;
}

void renderer_applies_nested_value_clips() {
    ScriptTaskFrameRenderer renderer;
    ScriptTaskFrameRenderStatus status = ScriptTaskFrameRenderStatus::InvalidFrame;
    const FrameBuffer output = renderer.render(rounded_frame(), {255, 255, 255, 255}, &status);

    assert(status == ScriptTaskFrameRenderStatus::Accepted);
    assert(output.pixel(8, 8).r == 255 && output.pixel(8, 8).g == 255);
    assert(output.pixel(12, 12).r == 255 && output.pixel(12, 12).g == 255);
    assert(output.pixel(20, 20).b > 200);
}

void renderer_keeps_non_dirty_pixels_and_rejects_bad_chain() {
    ScriptTaskFrameRenderer renderer;
    ScriptTaskAppFrame frame = rounded_frame();
    FrameBuffer output(40, 40, {90, 90, 90, 255});
    const Rect dirty{0, 0, 20, 40};
    ScriptTaskFrameRenderStatus status = ScriptTaskFrameRenderStatus::InvalidFrame;
    assert(renderer.render_into(frame, output, {255, 255, 255, 255}, &dirty, 1, nullptr, &status));
    assert(status == ScriptTaskFrameRenderStatus::Accepted);
    assert(output.pixel(30, 30).r == 90);

    frame.display_clip_indices[0] = 7;
    assert(!renderer.render_into(frame, output, {255, 255, 255, 255}, nullptr, 0, nullptr, &status));
    assert(status == ScriptTaskFrameRenderStatus::InvalidClipChain);
}

} // namespace

int script_task_frame_renderer_tests_main() {
    renderer_applies_nested_value_clips();
    renderer_keeps_non_dirty_pixels_and_rejects_bad_chain();
    std::cout << "script task frame renderer tests passed\n";
    return 0;
}
