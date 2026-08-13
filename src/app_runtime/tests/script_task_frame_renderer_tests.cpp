#include "app_runtime/script_task_frame_renderer.h"
#include "app_runtime/app_font_set.h"

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

    current = previous;
    current.display_clip_indices.clear();
    current.display_clip_indices.push_back(0);
    const ScriptTaskFrameDiff malformed_report = diff_script_task_app_frames(previous, current);
    assert(!malformed_report.display_clip_indices_equal && !malformed_report.paint_structure_equal);
    assert(malformed_report.unchanged_command_count == 0 && malformed_report.changed_command_count == 1);
}

void frame_diff_accumulator_owns_only_accepted_value_frames() {
    ScriptTaskAppFrame base = rounded_frame();
    DisplayCommand badge;
    badge.type = DisplayCommandType::FillRect;
    badge.rect = {4, 4, 8, 8};
    badge.color = {20, 30, 40, 255};
    base.display_list.push_back(badge);
    base.display_clip_indices.push_back(0);
    ScriptTaskFrameDiffAccumulator accumulator;

    assert(!accumulator.observe_presented(ScriptTaskAppFrame(base)));
    ScriptTaskAppFrame first_mutation = base;
    first_mutation.display_list[1].color = {50, 60, 70, 255};
    assert(accumulator.observe_presented(std::move(first_mutation)));
    ScriptTaskAppFrame second_mutation = base;
    second_mutation.display_list[1].color = {80, 90, 100, 255};
    assert(accumulator.observe_presented(std::move(second_mutation)));

    const ScriptTaskFrameDiffStatistics& statistics = accumulator.statistics();
    assert(statistics.pairs == 2 && statistics.paint_structure_equal_pairs == 2 &&
           statistics.input_targets_equal_pairs == 2);
    assert(statistics.unchanged_commands == 2 && statistics.changed_commands == 2 &&
           statistics.unchanged_prefix_commands == 2 && statistics.unchanged_suffix_commands == 0);
    assert(statistics.changed_bounds_pairs == 2 && statistics.changed_bounds_pixels == 128);

    accumulator.reset();
    assert(accumulator.statistics().pairs == 0 && accumulator.statistics().changed_bounds_pixels == 0);
    assert(!accumulator.observe_presented(ScriptTaskAppFrame(base)));
}

bool equal_framebuffer_pixels(const FrameBuffer& left, const FrameBuffer& right) {
    if (left.width != right.width || left.height != right.height || left.pixels.size() != right.pixels.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.pixels.size(); ++index) {
        const Color lhs = left.pixels[index];
        const Color rhs = right.pixels[index];
        if (lhs.r != rhs.r || lhs.g != rhs.g || lhs.b != rhs.b || lhs.a != rhs.a) {
            return false;
        }
    }
    return true;
}

ScriptTaskAppFrame replay_candidate_frame() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 40, 24};
    DisplayCommand background;
    background.type = DisplayCommandType::FillRect;
    background.rect = {0, 0, 40, 24};
    background.color = {24, 40, 68, 255};
    frame.display_list.push_back(background);
    DisplayCommand card;
    card.type = DisplayCommandType::LinearGradient;
    card.rect = {4, 3, 32, 18};
    card.color = {60, 180, 220, 255};
    card.color2 = {40, 100, 180, 255};
    card.border_radius = 6;
    frame.display_list.push_back(card);
    DisplayCommand overlay;
    overlay.type = DisplayCommandType::FillRect;
    overlay.rect = {12, 8, 12, 8};
    overlay.color = {250, 90, 70, 128};
    overlay.border_radius = 3;
    frame.display_list.push_back(overlay);
    frame.clips = {{{4, 3, 32, 18}, 6, kScriptTaskNoParentClip}};
    frame.display_clip_indices = {kScriptTaskNoClip, 0, 0};
    return frame;
}

void retained_replay_rebuilds_stable_underlap_and_verifies_pixels() {
    ScriptTaskFrameRenderer renderer;
    ScriptTaskFrameRetainedReplayOptions options;
    options.enabled = true;
    options.max_retained_pixels = 40 * 24;
    options.max_replay_pixels = 40 * 24;
    ScriptTaskFrameRetainedReplay replay(options);
    const Color background{255, 255, 255, 255};

    ScriptTaskAppFrame previous = replay_candidate_frame();
    const FrameBuffer previous_image = renderer.render(previous, background);
    assert(replay.observe_presented(previous, previous_image, background, 9));

    ScriptTaskAppFrame current = previous;
    current.display_list[2].color = {80, 240, 110, 128};
    FrameBuffer actual;
    ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled;
    assert(replay.render_into(renderer, current, actual, background, 9, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::Replayed);
    const FrameBuffer expected = renderer.render(current, background);
    assert(equal_framebuffer_pixels(actual, expected));
    // The translucent overlay must be composited over the stable gradient,
    // proving that replay repaints the unchanged underlap after clearing.
    assert(actual.pixel(15, 10).g > actual.pixel(15, 10).r);

    const ScriptTaskFrameRetainedReplayStatistics& statistics = replay.statistics();
    assert(statistics.candidates == 1 && statistics.replays == 1 &&
           statistics.pixel_mismatch_fallbacks == 0 && statistics.replayed_command_groups == 2 &&
           statistics.replayed_commands == 3 &&
           statistics.retained_image_pixels == 40 * 24 && statistics.candidate_image_pixels == 40 * 24 &&
           statistics.retained_image_bytes == 40 * 24 * sizeof(Color) &&
           statistics.candidate_image_bytes == 40 * 24 * sizeof(Color) &&
           statistics.canonical_output_bytes == 40 * 24 * sizeof(Color));
}

void retained_replay_uses_conservative_fallbacks_and_commit_boundary() {
    ScriptTaskFrameRenderer renderer;
    ScriptTaskFrameRetainedReplayOptions options;
    options.enabled = true;
    options.max_retained_pixels = 40 * 24;
    options.max_replay_pixels = 40 * 24;
    ScriptTaskFrameRetainedReplay replay(options);
    const Color background{255, 255, 255, 255};
    ScriptTaskAppFrame previous = replay_candidate_frame();
    ScriptTaskAppFrame current = previous;
    current.display_list[2].color = {30, 220, 80, 128};

    FrameBuffer output(40, 24, background);
    ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::Replayed;
    assert(replay.render_into(renderer, current, output, background, 7, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::FullFrameNoPrevious);

    const FrameBuffer previous_image = renderer.render(previous, background);
    assert(replay.observe_presented(previous, previous_image, background, 7));
    assert(replay.render_into(renderer, current, output, background, 8, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::FullFrameIneligible);
    assert(replay.statistics().full_frame_no_previous == 1 &&
           replay.statistics().full_frame_ineligible == 1 && replay.statistics().candidates == 0);

    replay.reset();
    options.enabled = false;
    ScriptTaskFrameRetainedReplay disabled(options);
    assert(disabled.render_into(renderer, current, output, background, 7, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled);
    assert(disabled.statistics().full_frame_disabled == 1);
}

void retained_replay_rejects_unbounded_host_painters() {
    DisplayCommand text;
    text.type = DisplayCommandType::Text;
    text.rect = {1, 2, 12, 8};
    Rect bounds;
    assert(!script_task_frame_command_visual_bounds(text, bounds));
    assert(bounds.width == 0 && bounds.height == 0);

    DisplayCommand image;
    image.type = DisplayCommandType::Image;
    image.rect = {1, 2, 12, 8};
    assert(!script_task_frame_command_visual_bounds(image, bounds));

    DisplayCommand shadow;
    shadow.type = DisplayCommandType::BoxShadow;
    shadow.rect = {1, 2, 12, 8};
    assert(script_task_frame_command_visual_bounds(shadow, bounds));
    assert(bounds.x == 1 && bounds.y == 2 && bounds.width == 12 && bounds.height == 8);
}

bool bounded_marker_text_painter(FrameBuffer& target,
                                 Rect rect,
                                 Color color,
                                 const std::string& text,
                                 int,
                                 int,
                                 TextCommandAlign,
                                 bool,
                                 void*) {
    if (rect.width <= 0 || rect.height <= 0 || text.empty() || !target.contains(rect.x, rect.y)) {
        return false;
    }
    target.pixel(rect.x, rect.y) = color;
    return true;
}

ScriptTaskAppFrame text_replay_frame() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 24, 12};
    DisplayCommand fill;
    fill.type = DisplayCommandType::FillRect;
    fill.rect = {0, 0, 24, 12};
    fill.color = {20, 30, 40, 255};
    frame.display_list.push_back(fill);
    DisplayCommand text;
    text.type = DisplayCommandType::Text;
    text.rect = {7, 4, 8, 4};
    text.color = {80, 220, 130, 255};
    text.text = "1";
    frame.display_list.push_back(text);
    return frame;
}

void retained_replay_accepts_explicitly_bounded_text_painter() {
    TextPainter bounded{bounded_marker_text_painter, nullptr, nullptr, true};
    ScriptTaskFrameRenderer renderer(bounded);
    ScriptTaskFrameRetainedReplayOptions options;
    options.enabled = true;
    options.max_retained_pixels = 24 * 12;
    options.max_replay_pixels = 24 * 12;
    ScriptTaskFrameRetainedReplay replay(options);
    const Color background{255, 255, 255, 255};
    ScriptTaskAppFrame previous = text_replay_frame();
    const FrameBuffer previous_image = renderer.render(previous, background);
    assert(replay.observe_presented(previous, previous_image, background, 2));

    ScriptTaskAppFrame current = previous;
    current.display_list[1].text = "2";
    FrameBuffer actual;
    ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled;
    assert(replay.render_into(renderer, current, actual, background, 2, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::Replayed);
    assert(equal_framebuffer_pixels(actual, renderer.render(current, background)));

    ScriptTaskFrameRenderer unbounded_renderer(TextPainter{bounded_marker_text_painter, nullptr});
    ScriptTaskFrameRetainedReplay unbounded_replay(options);
    const FrameBuffer unbounded_previous = unbounded_renderer.render(previous, background);
    assert(unbounded_replay.observe_presented(previous, unbounded_previous, background, 2));
    assert(unbounded_replay.render_into(unbounded_renderer, current, actual, background, 2, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::FullFrameIneligible);
    assert(unbounded_replay.statistics().ineligible_by_reason[
               static_cast<std::size_t>(ScriptTaskFrameRetainedReplayFallbackReason::UnsupportedVisualBounds)] == 1);
}

const BitmapFont& retained_replay_test_font() {
    static constexpr std::uint8_t rows[] = {
        0b01000000,
        0b10100000,
        0b11100000,
        0b10100000,
        0b10100000,
    };
    static constexpr BitmapFontGlyph glyphs[] = {
        BitmapFontGlyph{0x41, 3, 5, 4, 1, rows},
    };
    static constexpr BitmapFont font{glyphs, 1, 5, 4};
    return font;
}

void retained_replay_accepts_verified_app_font_set_painter() {
    AppFontSet fonts;
    fonts.set_system_font(&retained_replay_test_font());
    const TextPainter painter = fonts.painter();
    assert(painter.writes_only_within_rect);
    ScriptTaskFrameRenderer renderer(painter);
    ScriptTaskFrameRetainedReplayOptions options;
    options.enabled = true;
    options.max_retained_pixels = 24 * 12;
    options.max_replay_pixels = 24 * 12;
    ScriptTaskFrameRetainedReplay replay(options);
    const Color background{255, 255, 255, 255};
    ScriptTaskAppFrame previous = text_replay_frame();
    previous.display_list[1].text = "A";
    previous.display_list[1].font_weight = 700;
    const FrameBuffer previous_image = renderer.render(previous, background);
    assert(replay.observe_presented(previous, previous_image, background, 5));

    ScriptTaskAppFrame current = previous;
    current.display_list[1].color = {240, 100, 70, 255};
    FrameBuffer actual;
    ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled;
    assert(replay.render_into(renderer, current, actual, background, 5, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::Replayed);
    assert(equal_framebuffer_pixels(actual, renderer.render(current, background)));
    assert(replay.statistics().replays == 1 && replay.statistics().pixel_mismatch_fallbacks == 0);
}

bool bounded_marker_image_painter(FrameBuffer& target,
                                  Rect rect,
                                  std::uint32_t image_handle,
                                  ObjectFit,
                                  ObjectPosition,
                                  ImageRendering,
                                  void*) {
    if (image_handle == 0 || rect.width <= 0 || rect.height <= 0 || !target.contains(rect.x, rect.y)) {
        return false;
    }
    target.pixel(rect.x, rect.y) = {220, 110, 70, 255};
    return true;
}

void retained_replay_accepts_explicitly_bounded_image_painter() {
    ImagePainter bounded{bounded_marker_image_painter, nullptr, true};
    ScriptTaskFrameRenderer renderer({}, bounded);
    ScriptTaskFrameRetainedReplayOptions options;
    options.enabled = true;
    options.max_retained_pixels = 24 * 12;
    options.max_replay_pixels = 24 * 12;
    ScriptTaskFrameRetainedReplay replay(options);
    const Color background{255, 255, 255, 255};
    ScriptTaskAppFrame previous = text_replay_frame();
    previous.display_list[1].type = DisplayCommandType::Image;
    previous.display_list[1].image_handle = 7;
    previous.display_list[1].text.clear();
    const FrameBuffer previous_image = renderer.render(previous, background);
    assert(replay.observe_presented(previous, previous_image, background, 3));

    ScriptTaskAppFrame current = previous;
    current.display_list[1].object_position.x_percent = 75;
    FrameBuffer actual;
    ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled;
    assert(replay.render_into(renderer, current, actual, background, 3, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::Replayed);
    assert(equal_framebuffer_pixels(actual, renderer.render(current, background)));
}

bool lying_text_painter(FrameBuffer& target,
                         Rect rect,
                         Color color,
                         const std::string& text,
                         int,
                         int,
                         TextCommandAlign,
                         bool,
                         void*) {
    if (text.empty()) {
        return true;
    }
    const int x = rect.x - 1;
    if (!target.contains(x, rect.y)) {
        return false;
    }
    target.pixel(x, rect.y) = color;
    return true;
}

void retained_replay_recovers_from_false_bounded_painter_claim() {
    TextPainter lying{lying_text_painter, nullptr, nullptr, true};
    ScriptTaskFrameRenderer renderer(lying);
    ScriptTaskFrameRetainedReplayOptions options;
    options.enabled = true;
    options.max_retained_pixels = 24 * 12;
    options.max_replay_pixels = 24 * 12;
    ScriptTaskFrameRetainedReplay replay(options);
    const Color background{255, 255, 255, 255};
    ScriptTaskAppFrame previous = text_replay_frame();
    const FrameBuffer previous_image = renderer.render(previous, background);
    assert(replay.observe_presented(previous, previous_image, background, 4));

    ScriptTaskAppFrame current = previous;
    current.display_list[1].text.clear();
    FrameBuffer actual;
    ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::Replayed;
    assert(replay.render_into(renderer, current, actual, background, 4, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::PixelMismatchFallback);
    assert(equal_framebuffer_pixels(actual, renderer.render(current, background)));
    assert(replay.statistics().candidates == 1 && replay.statistics().replays == 0 &&
           replay.statistics().pixel_mismatch_fallbacks == 1 &&
           replay.statistics().pixel_mismatch_pixels == 1 &&
           replay.statistics().has_first_pixel_mismatch &&
           replay.statistics().first_pixel_mismatch_x == 6 &&
           replay.statistics().first_pixel_mismatch_y == 4);
}

ScriptTaskAppFrame retained_replay_stress_frame() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 64, 48};

    DisplayCommand background;
    background.type = DisplayCommandType::FillRect;
    background.rect = {0, 0, 64, 48};
    background.color = {16, 24, 38, 255};
    frame.display_list.push_back(background);

    DisplayCommand shadow;
    shadow.type = DisplayCommandType::BoxShadow;
    shadow.rect = {6, 5, 52, 38};
    shadow.color = {0, 0, 0, 92};
    shadow.border_radius = 10;
    shadow.stroke_width = 5;
    frame.display_list.push_back(shadow);

    DisplayCommand surface;
    surface.type = DisplayCommandType::LinearGradient;
    surface.rect = {10, 8, 44, 30};
    surface.color = {58, 184, 214, 255};
    surface.color2 = {52, 84, 182, 255};
    surface.gradient_axis = GradientAxis::DiagonalDownRight;
    surface.gradient_stop_percent = 72;
    surface.border_radius = 8;
    frame.display_list.push_back(surface);

    DisplayCommand accent;
    accent.type = DisplayCommandType::FillRect;
    accent.rect = {18, 19, 10, 7};
    accent.color = {247, 105, 81, 144};
    accent.border_radius = 3;
    frame.display_list.push_back(accent);

    frame.clips = {{{10, 8, 44, 30}, 8, kScriptTaskNoParentClip}};
    frame.display_clip_indices = {kScriptTaskNoClip, kScriptTaskNoClip, 0, 0};
    return frame;
}

ScriptTaskFrameRetainedReplayOptions retained_replay_options_for(Rect viewport) {
    ScriptTaskFrameRetainedReplayOptions options;
    options.enabled = true;
    options.max_retained_pixels = static_cast<std::size_t>(viewport.width) *
        static_cast<std::size_t>(viewport.height);
    options.max_replay_pixels = options.max_retained_pixels;
    return options;
}

void retained_replay_matches_canonical_output_across_local_mutations() {
    ScriptTaskFrameRenderer renderer;
    ScriptTaskAppFrame previous = retained_replay_stress_frame();
    const Color background{239, 244, 250, 255};
    ScriptTaskFrameRetainedReplay replay(retained_replay_options_for(previous.viewport));
    FrameBuffer presented = renderer.render(previous, background);
    assert(replay.observe_presented(previous, presented, background, 11));

    constexpr int kIterations = 96;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        ScriptTaskAppFrame current = previous;
        DisplayCommand& accent = current.display_list[3];
        accent.rect.x = 14 + (iteration % 18);
        accent.rect.y = 14 + ((iteration * 3) % 12);
        accent.color = {static_cast<std::uint8_t>(70 + (iteration * 17) % 150),
                        static_cast<std::uint8_t>(90 + (iteration * 11) % 140),
                        static_cast<std::uint8_t>(100 + (iteration * 7) % 130),
                        static_cast<std::uint8_t>(80 + (iteration % 6) * 25)};

        FrameBuffer actual;
        ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled;
        assert(replay.render_into(renderer, current, actual, background, 11, &status));
        assert(status == ScriptTaskFrameRetainedReplayStatus::Replayed);
        assert(equal_framebuffer_pixels(actual, renderer.render(current, background)));
        assert(replay.observe_presented(current, actual, background, 11));
        previous = std::move(current);
    }

    const ScriptTaskFrameRetainedReplayStatistics& statistics = replay.statistics();
    assert(statistics.candidates == kIterations && statistics.replays == kIterations &&
           statistics.pixel_mismatch_fallbacks == 0 && statistics.pixel_mismatch_pixels == 0 &&
           !statistics.has_first_pixel_mismatch && statistics.replayed_command_groups >= kIterations &&
           statistics.replayed_commands >= static_cast<std::uint64_t>(kIterations) * 2);
}

void retained_replay_falls_back_for_contract_boundary_changes() {
    ScriptTaskFrameRenderer renderer;
    const Color background{239, 244, 250, 255};
    const ScriptTaskAppFrame base = retained_replay_stress_frame();
    const auto verify_fallback = [&](const ScriptTaskAppFrame& current,
                                     Color current_background,
                                     std::uint64_t current_generation,
                                     std::size_t max_replay_pixels,
                                     ScriptTaskFrameRetainedReplayFallbackReason expected_reason) {
        ScriptTaskFrameRetainedReplayOptions options = retained_replay_options_for(base.viewport);
        options.max_replay_pixels = max_replay_pixels;
        ScriptTaskFrameRetainedReplay replay(options);
        const FrameBuffer previous_image = renderer.render(base, background);
        assert(replay.observe_presented(base, previous_image, background, 11));

        FrameBuffer expected(current.viewport.width, current.viewport.height, current_background);
        ScriptTaskFrameRenderStatus expected_status = ScriptTaskFrameRenderStatus::InvalidFrame;
        assert(renderer.render_into(current, expected, current_background, nullptr, 0, nullptr, &expected_status));
        assert(expected_status == ScriptTaskFrameRenderStatus::Accepted);
        FrameBuffer actual;
        ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::Replayed;
        ScriptTaskFrameRetainedReplayFallbackReason reason = ScriptTaskFrameRetainedReplayFallbackReason::None;
        assert(replay.render_into(renderer, current, actual, current_background, current_generation, &status, &reason));
        assert(status == ScriptTaskFrameRetainedReplayStatus::FullFrameIneligible);
        assert(reason == expected_reason);
        assert(equal_framebuffer_pixels(actual, expected));
        assert(replay.statistics().candidates == 0 && replay.statistics().replays == 0);
        assert(replay.statistics().ineligible_by_reason[static_cast<std::size_t>(expected_reason)] == 1);
    };

    ScriptTaskAppFrame locally_changed = base;
    locally_changed.display_list[3].rect = {30, 18, 10, 7};
    verify_fallback(locally_changed,
                    background,
                    12,
                    64 * 48,
                    ScriptTaskFrameRetainedReplayFallbackReason::ResourceGeneration);
    verify_fallback(locally_changed,
                    {250, 250, 250, 255},
                    11,
                    64 * 48,
                    ScriptTaskFrameRetainedReplayFallbackReason::Background);
    verify_fallback(locally_changed,
                    background,
                    11,
                    1,
                    ScriptTaskFrameRetainedReplayFallbackReason::ReplayRegionBudget);

    ScriptTaskAppFrame changed_clip = base;
    changed_clip.clips[0].border_radius = 4;
    verify_fallback(changed_clip,
                    background,
                    11,
                    64 * 48,
                    ScriptTaskFrameRetainedReplayFallbackReason::PaintSkeleton);

    ScriptTaskAppFrame changed_type = base;
    changed_type.display_list[3].type = DisplayCommandType::StrokeRect;
    verify_fallback(changed_type,
                    background,
                    11,
                    64 * 48,
                    ScriptTaskFrameRetainedReplayFallbackReason::PaintSkeleton);

    ScriptTaskAppFrame appended = base;
    appended.display_list.push_back(base.display_list[3]);
    appended.display_clip_indices.push_back(0);
    verify_fallback(appended,
                    background,
                    11,
                    64 * 48,
                    ScriptTaskFrameRetainedReplayFallbackReason::PaintSkeleton);

    verify_fallback(base,
                    background,
                    11,
                    64 * 48,
                    ScriptTaskFrameRetainedReplayFallbackReason::NoChangedCommands);
}

std::uint32_t next_replay_test_random(std::uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}

void retained_replay_matches_canonical_output_across_pseudorandom_mutations() {
    ScriptTaskFrameRenderer renderer;
    const Color background{239, 244, 250, 255};
    ScriptTaskAppFrame previous = retained_replay_stress_frame();
    ScriptTaskFrameRetainedReplay replay(retained_replay_options_for(previous.viewport));
    const FrameBuffer initial = renderer.render(previous, background);
    assert(replay.observe_presented(previous, initial, background, 11));

    std::uint32_t random = 0x5EED1234U;
    constexpr int kIterations = 192;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        ScriptTaskAppFrame current = previous;
        const std::uint32_t value = next_replay_test_random(random);
        DisplayCommand& command = current.display_list[value % current.display_list.size()];
        switch (value % 4U) {
        case 0:
            command.color = {static_cast<std::uint8_t>(value),
                             static_cast<std::uint8_t>(value >> 8U),
                             static_cast<std::uint8_t>(value >> 16U), 255};
            break;
        case 1:
            command.color = {static_cast<std::uint8_t>(value >> 3U),
                             static_cast<std::uint8_t>(value >> 11U),
                             static_cast<std::uint8_t>(value >> 19U),
                             static_cast<std::uint8_t>(48U + (value % 160U))};
            break;
        case 2:
            command.color2 = {static_cast<std::uint8_t>(value >> 2U),
                              static_cast<std::uint8_t>(value >> 10U),
                              static_cast<std::uint8_t>(value >> 18U), 255};
            command.gradient_stop_percent = 20 + static_cast<int>(value % 81U);
            break;
        default:
            command.rect.x = 10 + static_cast<int>(value % 36U);
            command.rect.y = 8 + static_cast<int>((value >> 8U) % 28U);
            break;
        }

        FrameBuffer actual;
        ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled;
        assert(replay.render_into(renderer, current, actual, background, 11, &status));
        assert(status == ScriptTaskFrameRetainedReplayStatus::Replayed);
        assert(equal_framebuffer_pixels(actual, renderer.render(current, background)));
        assert(replay.observe_presented(current, actual, background, 11));
        previous = std::move(current);
    }
    assert(replay.statistics().candidates == kIterations && replay.statistics().replays == kIterations &&
           replay.statistics().pixel_mismatch_fallbacks == 0);
}

void retained_replay_preserves_last_presented_state_across_rejections() {
    ScriptTaskFrameRenderer renderer;
    const Color background{239, 244, 250, 255};
    const ScriptTaskAppFrame base = retained_replay_stress_frame();
    ScriptTaskFrameRetainedReplay replay(retained_replay_options_for(base.viewport));
    const FrameBuffer initial = renderer.render(base, background);
    assert(replay.observe_presented(base, initial, background, 11));

    ScriptTaskAppFrame malformed = base;
    malformed.display_clip_indices.pop_back();
    FrameBuffer rejected;
    ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::Replayed;
    ScriptTaskFrameRetainedReplayFallbackReason reason = ScriptTaskFrameRetainedReplayFallbackReason::None;
    assert(!replay.render_into(renderer, malformed, rejected, background, 11, &status, &reason));
    assert(status == ScriptTaskFrameRetainedReplayStatus::RenderRejected);
    assert(reason == ScriptTaskFrameRetainedReplayFallbackReason::InvalidFrame);
    assert(!replay.observe_presented(malformed, rejected, background, 11));

    ScriptTaskAppFrame uncommitted = base;
    uncommitted.display_list[3].color = {210, 80, 100, 144};
    FrameBuffer uncommitted_image;
    assert(replay.render_into(renderer, uncommitted, uncommitted_image, background, 11, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::Replayed);
    // Simulate a failed present: no observe_presented() call is allowed here.

    ScriptTaskAppFrame current = base;
    current.display_list[3].color = {90, 218, 148, 144};
    FrameBuffer actual;
    assert(replay.render_into(renderer, current, actual, background, 11, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::Replayed);
    assert(equal_framebuffer_pixels(actual, renderer.render(current, background)));
    assert(replay.statistics().full_frame_rejected == 1 && replay.statistics().replays == 2 &&
           replay.statistics().pixel_mismatch_fallbacks == 0);
    assert(replay.statistics().ineligible_by_reason[
               static_cast<std::size_t>(ScriptTaskFrameRetainedReplayFallbackReason::InvalidFrame)] == 1);
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
    frame_diff_accumulator_owns_only_accepted_value_frames();
    retained_replay_rebuilds_stable_underlap_and_verifies_pixels();
    retained_replay_uses_conservative_fallbacks_and_commit_boundary();
    retained_replay_rejects_unbounded_host_painters();
    retained_replay_accepts_explicitly_bounded_text_painter();
    retained_replay_accepts_verified_app_font_set_painter();
    retained_replay_accepts_explicitly_bounded_image_painter();
    retained_replay_recovers_from_false_bounded_painter_claim();
    retained_replay_matches_canonical_output_across_local_mutations();
    retained_replay_falls_back_for_contract_boundary_changes();
    retained_replay_matches_canonical_output_across_pseudorandom_mutations();
    retained_replay_preserves_last_presented_state_across_rejections();
    std::cout << "script task frame renderer tests passed\n";
    return 0;
}
