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
    const std::uint64_t samples[] = {40, 42, 49, 58, 58, 60, 61, 62};
    ReplayTimingClock clock{samples, 8, 0};
    SoftwareRasterizerStatistics statistics;
    ScriptTaskFrameRendererOptions options;
    options.rasterizer_statistics = &statistics;
    options.rasterizer_timing = {replay_timing_clock, &clock};
    ScriptTaskFrameRenderer renderer({}, options);

    ScriptTaskFrameRenderStatus status = ScriptTaskFrameRenderStatus::InvalidFrame;
    const FrameBuffer output = renderer.render(rounded_frame(), {255, 255, 255, 255}, &status);

    assert(status == ScriptTaskFrameRenderStatus::Accepted);
    assert(!output.pixels.empty());
    assert(clock.calls == 8);
    assert(statistics.rounded_clip_replay_microseconds_by_type[
               static_cast<std::size_t>(DisplayCommandType::FillRect)] == 9);
    assert(statistics.rounded_clip_replay_microseconds == 9);
    assert(statistics.rounded_clip_surface_prepare_microseconds == 2);
    assert(statistics.rounded_clip_composite_microseconds == 4);
    assert(statistics.rounded_clip_coverage_sampled_composite_microseconds == 3);
    assert(statistics.rounded_clip_full_coverage_composite_microseconds == 1);
    assert(statistics.rounded_clip_coverage_sampled_composite_microseconds +
               statistics.rounded_clip_full_coverage_composite_microseconds ==
           statistics.rounded_clip_composite_microseconds);
}

void frame_diff_reports_value_churn_without_granting_reuse() {
    ScriptTaskAppFrame previous = rounded_frame();
    previous.input_targets.push_back({7, {14, 14, 10, 10}, true, 1});
    ScriptTaskAppFrame current = previous;
    current.display_list[0].color = {40, 140, 220, 255};
    current.input_targets[0].enabled = false;

    const ScriptTaskFrameDiff report = diff_script_task_app_frames(previous, current);
    assert(report.viewport_equal && report.clip_chains_equal && report.display_clip_indices_equal);
    assert(report.paint_structure_equal);
    assert(!report.input_targets_equal);
    assert(report.previous_command_count == 1 && report.current_command_count == 1);
    assert(report.unchanged_command_count == 0 && report.changed_command_count == 1);
    assert(report.unchanged_prefix_command_count == 0 && report.unchanged_suffix_command_count == 0);
    assert(report.has_changed_command_bounds && report.changed_command_bounds.x == 0 &&
           report.changed_command_bounds.y == 0 && report.changed_command_bounds.width == 40 &&
           report.changed_command_bounds.height == 40);

    current.clips[1].border_radius = 4;
    const ScriptTaskFrameDiff changed_clip_report = diff_script_task_app_frames(previous, current);
    assert(!changed_clip_report.clip_chains_equal && !changed_clip_report.paint_structure_equal);

    current = previous;
    DisplayCommand appended = previous.display_list[0];
    appended.rect = {4, 4, 6, 6};
    current.display_list.push_back(appended);
    current.display_clip_indices.push_back(1);
    const ScriptTaskFrameDiff appended_report = diff_script_task_app_frames(previous, current);
    assert(!appended_report.display_clip_indices_equal && !appended_report.paint_structure_equal);
    assert(appended_report.unchanged_command_count == 1 && appended_report.changed_command_count == 1);
    assert(appended_report.unchanged_prefix_command_count == 1 && appended_report.unchanged_suffix_command_count == 0);
    assert(appended_report.has_changed_command_bounds && appended_report.changed_command_bounds.x == 4 &&
           appended_report.changed_command_bounds.y == 4 && appended_report.changed_command_bounds.width == 6 &&
           appended_report.changed_command_bounds.height == 6);
}

} // namespace

int script_task_frame_renderer_tests_main() {
    renderer_applies_nested_value_clips();
    renderer_consumes_codec_round_tripped_v2_frame();
    renderer_matches_layer_compositor_for_translucent_clip_run();
    renderer_keeps_non_dirty_pixels_and_rejects_bad_chain();
    renderer_exposes_rounded_dirty_fast_path_statistics();
    renderer_forwards_opt_in_rounded_replay_timing();
    frame_diff_reports_value_churn_without_granting_reuse();
    std::cout << "script task frame renderer tests passed\n";
    return 0;
}
