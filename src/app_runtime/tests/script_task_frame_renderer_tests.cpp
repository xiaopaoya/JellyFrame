#include "app_runtime/script_task_frame_renderer.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace jellyframe;

namespace {

struct ReplayTimingClock {
    const std::uint64_t* samples = nullptr;
    std::size_t sample_count = 0;
    std::size_t calls = 0;
};

std::uint64_t replay_timing_clock(void* raw_context) {
    auto* clock = static_cast<ReplayTimingClock*>(raw_context);
    if (clock == nullptr || clock->samples == nullptr || clock->sample_count == 0) {
        return 0;
    }
    const std::size_t index = clock->calls < clock->sample_count
        ? clock->calls
        : clock->sample_count - 1;
    ++clock->calls;
    return clock->samples[index];
}

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

void renderer_consumes_codec_round_tripped_v2_frame() {
    ScriptTaskAppFrameCodecOptions options;
    options.version = 2;
    options.max_commands = 4;
    options.max_text_bytes = 128;
    options.max_input_targets = 4;
    options.max_payload_bytes = 4096;
    options.max_clips = 4;
    options.max_clip_depth = 4;

    std::vector<std::uint8_t> encoded;
    assert(encode_script_task_app_frame(rounded_frame(), options, encoded) ==
           ScriptTaskAppFrameCodecStatus::Accepted);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(encoded, options, decoded) ==
           ScriptTaskAppFrameCodecStatus::Accepted);

    ScriptTaskFrameRenderer renderer;
    ScriptTaskFrameRenderStatus status = ScriptTaskFrameRenderStatus::InvalidFrame;
    const FrameBuffer output = renderer.render(decoded, {255, 255, 255, 255}, &status);
    assert(status == ScriptTaskFrameRenderStatus::Accepted);
    assert(output.pixel(8, 8).r == 255 && output.pixel(8, 8).g == 255);
    assert(output.pixel(20, 20).b > 200);
}

void renderer_matches_layer_compositor_for_translucent_clip_run() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = {0, 0, 40, 40};
    LayerNodePtr clipped(new LayerNode(), LayerNodeDeleter{});
    clipped->type = LayerType::Clip;
    clipped->has_clip = true;
    clipped->clip_rect = {8, 8, 24, 24};
    clipped->clip_border_radius = 8;
    DisplayCommand blue;
    blue.type = DisplayCommandType::FillRect;
    blue.rect = {0, 0, 40, 40};
    blue.color = {20, 120, 240, 160};
    DisplayCommand red;
    red.type = DisplayCommandType::FillRect;
    red.rect = {12, 12, 20, 20};
    red.color = {240, 80, 70, 128};
    clipped->display_list.push_back(blue);
    clipped->display_list.push_back(red);
    root.children.push_back(std::move(clipped));

    const FrameBuffer expected = SoftwareCompositor().render(root, 40, 40, {255, 255, 255, 255});
    const ScriptTaskAppFrame frame = make_script_task_app_frame(root, {0, 0, 40, 40}, {}, true);
    ScriptTaskFrameRenderer renderer;
    const FrameBuffer actual = renderer.render(frame, {255, 255, 255, 255});
    assert(actual.pixels.size() == expected.pixels.size());
    for (std::size_t index = 0; index < actual.pixels.size(); ++index) {
        const Color left = actual.pixels[index];
        const Color right = expected.pixels[index];
        assert(left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a);
    }
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

void renderer_exposes_rounded_dirty_fast_path_statistics() {
    ScriptTaskAppFrame frame = rounded_frame();
    FrameBuffer output(40, 40, {255, 255, 255, 255});
    SoftwareRasterizerStatistics statistics;
    ScriptTaskFrameRendererOptions options;
    options.rasterizer_statistics = &statistics;
    ScriptTaskFrameRenderer renderer({}, options);
    const Rect dirty{16, 18, 8, 4};
    ScriptTaskFrameRenderStatus status = ScriptTaskFrameRenderStatus::InvalidFrame;

    assert(renderer.render_into(frame, output, {255, 255, 255, 255}, &dirty, 1, nullptr, &status));
    assert(status == ScriptTaskFrameRenderStatus::Accepted);
    assert(statistics.rounded_clip_rectangular_fast_paths == 1);
    assert(statistics.rounded_clip_runs == 0);
}

void renderer_forwards_opt_in_rounded_replay_timing() {
    const std::uint64_t samples[] = {40, 49};
    ReplayTimingClock clock{samples, 2, 0};
    SoftwareRasterizerStatistics statistics;
    ScriptTaskFrameRendererOptions options;
    options.rasterizer_statistics = &statistics;
    options.rasterizer_timing = {replay_timing_clock, &clock};
    ScriptTaskFrameRenderer renderer({}, options);

    ScriptTaskFrameRenderStatus status = ScriptTaskFrameRenderStatus::InvalidFrame;
    const FrameBuffer output = renderer.render(rounded_frame(), {255, 255, 255, 255}, &status);

    assert(status == ScriptTaskFrameRenderStatus::Accepted);
    assert(!output.pixels.empty());
    assert(clock.calls == 2);
    assert(statistics.rounded_clip_replay_microseconds_by_type[
               static_cast<std::size_t>(DisplayCommandType::FillRect)] == 9);
    assert(statistics.rounded_clip_replay_microseconds == 9);
}

} // namespace

int script_task_frame_renderer_tests_main() {
    renderer_applies_nested_value_clips();
    renderer_consumes_codec_round_tripped_v2_frame();
    renderer_matches_layer_compositor_for_translucent_clip_run();
    renderer_keeps_non_dirty_pixels_and_rejects_bad_chain();
    renderer_exposes_rounded_dirty_fast_path_statistics();
    renderer_forwards_opt_in_rounded_replay_timing();
    std::cout << "script task frame renderer tests passed\n";
    return 0;
}
