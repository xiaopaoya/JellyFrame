#include "app_runtime/script_task_command_access.h"
#include "app_runtime/script_task_frame_renderer.h"
#include "app_runtime/script_task_sampling.h"
#include "app_runtime/script_task_frame_groups.h"
#include "app_runtime/script_task_canonical_codec.h"
#include "runtime_shaped_text_test_fixture.h"
#include "runtime_font_test_fixture.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace jellyframe;

namespace {

#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API && JELLYFRAME_RUNTIME_IMAGE_API
class ScalarReplayImages final : public RuntimeImageProviderSource {
public:
    std::uint64_t epoch = 1;
    std::uint64_t generation() const noexcept override { return epoch; }
    bool contains(std::uint32_t handle) const noexcept override { return handle == 73; }
    bool resolve(std::string_view, RuntimeResolvedImage& output) const override {
        output = {73, 8, 8};
        return true;
    }
    bool paint(FrameBuffer& target, const RuntimeImagePaintRequest& request) const override {
        if (!contains(request.image_handle)) return false;
        for (int y = std::max(0, request.clip.y); y < std::min(target.height, request.clip.y + request.clip.height); ++y)
            for (int x = std::max(0, request.clip.x); x < std::min(target.width, request.clip.x + request.clip.width); ++x)
                if (x >= request.rect.x && y >= request.rect.y &&
                    x < request.rect.x + request.rect.width && y < request.rect.y + request.rect.height)
                    target.pixel(x, y) = {41, 97, 173, 255};
        return true;
    }
};

void installed_scalar_replay_pins_resources_and_preserves_tiles() {
    runtime_test_fonts::Fonts fonts;
    auto source = std::make_shared<ScalarReplayImages>();
    RuntimeImageProvider::Handle images;
    assert(RuntimeImageProvider::create(source, images) == RuntimeImageProviderStatus::Ready);
    const Color clear{251, 249, 247, 255};
    const auto same = [](const FrameBuffer& a, const FrameBuffer& b) {
        return a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size() &&
            std::equal(a.pixels.begin(), a.pixels.end(), b.pixels.begin(), [](Color x, Color y) {
                return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
            });
    };
    for (const bool nbsp : {false, true}) {
        TextFontRequest request;
        request.size_q1000 = 16000;
        request.word_spacing_q1000 = nbsp ? 750 : 0;
        const std::string text = nbsp ? "A\xc2\xa0" "B" : "AB";
        PreparedTextGlyph glyphs[3];
        TextRunMetrics metrics;
        assert(fonts.catalog->text_system().prepare(text, request, glyphs, 3, metrics) == TextSystemStatus::Ok);
        DisplayCommand prepared;
        detail::set_command_type(prepared, DisplayCommandType::GlyphRun);
        prepared.rect = {0, 0, 48, 32}; prepared.color = {197, 31, 61, 255};
        set_glyph_run_origin(prepared, 4, 24);
        assert(encode_prepared_text(glyphs, metrics.glyph_count, request.size_q1000,
            static_cast<std::uint32_t>(text.size()), prepared.text, 0, request.word_spacing_q1000));
        assert(static_cast<unsigned char>(prepared.text[4]) == (nbsp ? 4 : 3));
        LayerNode root; root.type = LayerType::Root; root.bounds = prepared.rect;
        LayerNodePtr child(new LayerNode, LayerNodeDeleter{});
        child->type = LayerType::Composited; child->bounds = root.bounds;
        child->has_clip = true; child->clip_rect = {2, 2, 42, 28}; child->clip_border_radius = 3;
        child->opacity = 0.75F;
        DisplayCommand image; detail::set_command_type(image, DisplayCommandType::Image);
        image.rect = {8, 4, 24, 12}; detail::command_image_handle(image) = 73;
        child->display_list = {image, prepared}; root.children.push_back(std::move(child));
        ScriptTaskAppFrameCodecOptions codec;
        codec.version = kRuntimeScalarFrameVersion; codec.image_provider_id = images->id();
        codec.max_commands = 8; codec.max_groups = 8; codec.max_text_bytes = 4096;
        codec.max_payload_bytes = 16384;
        ScriptTaskAppFrame frame;
        assert(make_script_task_group_frame(root, root.bounds, codec, frame) == ScriptTaskAppFrameCodecStatus::Accepted);
        ScriptTaskFrameRendererOptions options; options.text_catalog = fonts.catalog; options.image_provider = images;
        ScriptTaskFrameRenderer renderer(options);
        FrameBuffer full(48, 32, clear);
        assert(renderer.render_into(frame, full, clear));
        SoftwareCompositor::Options core_options;
        core_options.text_system = &fonts.catalog->text_system();
        SoftwareCompositor core(TextPainter{}, images->painter(), core_options);
        const auto expected = core.render(root, 48, 32, clear);
        assert(same(full, expected));
        assert(std::any_of(full.pixels.begin(), full.pixels.end(), [&](Color p) { return p.r != clear.r; }));
        FrameBuffer tiled(48, 32, clear);
        for (int y = 0; y < 32; y += 8) for (int x = 0; x < 48; x += 8) {
            const Rect tile{x, y, 8, 8};
            assert(renderer.render_into(frame, tiled, clear, &tile, 1));
        }
        assert(same(full, tiled));
        ScriptTaskSupervisor supervisor({{2, 24}, {1, 0}, {1, 16384, 16384}, 0, 0});
        auto session = supervisor.begin(901, fonts.catalog, images);
        ScriptTaskAppFramePublisher publisher(codec);
        assert(publisher.publish(supervisor, session, frame).accepted());
        ScriptTaskAcceptedAppFrame accepted;
        assert(take_script_task_app_frame(supervisor, session, codec, accepted) == ScriptTaskAppFrameTakeStatus::Accepted);
        supervisor.begin(902, {});
        ScriptTaskFrameRendererOptions stale;
        stale.text_catalog = runtime_test_fonts::Fonts{}.catalog;
        auto foreign_source = std::make_shared<ScalarReplayImages>();
        assert(RuntimeImageProvider::create(foreign_source, stale.image_provider) == RuntimeImageProviderStatus::Ready);
        ScriptTaskFrameRenderer stale_renderer(stale);
        FrameBuffer sentinel(48, 32, {1, 2, 3, 255}), before = sentinel;
        assert(!stale_renderer.render_into(frame, sentinel, clear) && same(before, sentinel));
        assert(stale_renderer.render_into(accepted, sentinel, clear) && same(full, sentinel));
        auto missing = frame; detail::command_image_handle(missing.display_list[0]) = 74;
        sentinel = before;
        assert(!renderer.render_into(missing, sentinel, clear) && same(before, sentinel));
        stale.image_provider = images;
        ScriptTaskFrameRenderer foreign_catalog(stale);
        assert(!foreign_catalog.render_into(frame, sentinel, clear) && same(before, sentinel));
        ++source->epoch;
        assert(!renderer.render_into(frame, sentinel, clear) && same(before, sentinel));
        --source->epoch;
        if (nbsp) {
            fonts.catalog.reset(); images.reset(); source.reset();
            assert(stale_renderer.render_into(accepted, sentinel, clear) && same(full, sentinel));
        }
    }
}
#endif

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
void abi4_v11_shaped_text_codec_renderer_and_retained_replay() {
    using namespace runtime_test_shaped_text;
    const auto same_color = [](Color a, Color b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    const auto same_pixels = [&](const FrameBuffer& a, const FrameBuffer& b) {
        return a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size() &&
            std::equal(a.pixels.begin(), a.pixels.end(), b.pixels.begin(), same_color);
    };
    Assets assets("regular", 0x1122334455667788ULL);
    auto binding = create_binding(assets);
    std::uint64_t work = 1024U * 1024U;
    std::string packet;
    runtime_shaped::Metrics metrics;
    assert(binding->prepare(16000, "A", work, packet, metrics) == runtime_shaped::Status::ok);
    assert(packet.size() >= 4 && packet.compare(0, 4, "JFSP") == 0);

    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 48, 32};
    DisplayCommand background; detail::set_command_type(background, DisplayCommandType::FillRect);
    background.rect = frame.viewport;
    background.color = {245, 245, 245, 255};
    frame.display_list.push_back(background);
    ScriptTaskFrameGroup root;
    root.type = LayerType::Root; root.bounds = frame.viewport;
    root.command_begin = 0; root.command_count = 1;
    frame.groups.push_back(root);
    assert(append_script_task_shaped_text(frame, 1, 3 * 1000, 22 * 1000,
        {2, 8, 20, 18}, {2, 8, 9, 18}, {15, 35, 55, 255}, packet) ==
        ScriptTaskAppFrameCodecStatus::Accepted);

    ScriptTaskAppFrameCodecOptions codec;
    codec.version = kRuntimeShapedFrameVersion; codec.max_commands = 4; codec.max_text_bytes = 4096;
    codec.max_input_targets = 4; codec.max_payload_bytes = 16384; codec.max_clips = 4;
    codec.max_groups = 4; codec.max_group_depth = 4;
    assert(script_task_app_frame_codec_supports_version(kRuntimeShapedFrameVersion) ==
           detail::canonical::selected_version(kRuntimeShapedFrameVersion));
    std::vector<std::uint8_t> wire;
    assert(encode_script_task_app_frame(frame, codec, wire) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(wire[4] == kRuntimeShapedFrameVersion);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(wire, codec, decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(decoded.shaped_text.size() == 1 && decoded.shaped_text[0].packet == packet);

    runtime_test_fonts::Fonts fonts;
    ScriptTaskSupervisorOptions supervisor_options;
    supervisor_options.frame_mailbox = {2, 256};
    supervisor_options.frame_leases = {2, 16384, 32768};
    ScriptTaskSupervisor supervisor(supervisor_options);
    const auto session = supervisor.begin(801, fonts.catalog, {}, binding);
    assert(session.valid());
    assert(supervisor.publish_frame(session, wire).accepted());
    ScriptTaskAcceptedAppFrame accepted;
    assert(take_script_task_app_frame(supervisor, session, codec, accepted) == ScriptTaskAppFrameTakeStatus::Accepted);
    assert(accepted.valid() && accepted.shaped_text_binding() == binding);

    ScriptTaskFrameRendererOptions renderer_options;
    renderer_options.shaped_text_binding = create_binding(Assets("regular", 0x0102030405060708ULL));
    ScriptTaskFrameRenderer renderer(renderer_options);
    const Color clear{245, 245, 245, 255};
    FrameBuffer image(48, 32, clear);
    assert(renderer.render_into(accepted, image, clear));
    for (int y = 0; y < image.height; ++y)
        for (int x = 11; x < image.width; ++x) assert(same_color(image.pixel(x, y), clear));

    ScriptTaskAppFrame changed = decoded;
    changed.shaped_text[0].color = {160, 20, 35, 255};
    const auto diff = diff_script_task_app_frames(decoded, changed);
    assert(diff.changed_command_count == 1 && diff.has_changed_command_bounds);
    assert(diff.changed_command_bounds.x == 2 && diff.changed_command_bounds.width == 9);

    ScriptTaskFrameRendererOptions retained_options;
    retained_options.shaped_text_binding = binding;
    ScriptTaskFrameRenderer retained_renderer(retained_options);
    ScriptTaskFrameRetainedReplay replay({true, 48 * 32, 48 * 32});
    assert(replay.observe_presented(decoded, image, clear, 7));
    FrameBuffer retained;
    ScriptTaskFrameRetainedReplayStatus replay_status;
    assert(replay.render_into(retained_renderer, changed, retained, clear, 7, &replay_status));
    assert(replay_status == ScriptTaskFrameRetainedReplayStatus::Replayed);
    FrameBuffer canonical(48, 32, clear);
    assert(retained_renderer.render_into(changed, canonical, clear));
    assert(same_pixels(retained, canonical));

    Assets wrong_assets("regular", 0x8877665544332211ULL);
    auto wrong_owner = create_binding(wrong_assets);
    renderer_options.shaped_text_binding = wrong_owner;
    ScriptTaskFrameRenderer wrong_renderer(renderer_options);
    FrameBuffer sentinel(48, 32, {1, 2, 3, 255});
    const auto before = sentinel;
    assert(!wrong_renderer.render_into(decoded, sentinel, clear));
    assert(same_pixels(sentinel, before));

    ScriptTaskSupervisor wrong_supervisor(supervisor_options);
    const auto wrong_session = wrong_supervisor.begin(802, fonts.catalog, {}, wrong_owner);
    assert(wrong_session.valid() && wrong_supervisor.publish_frame(wrong_session, wire).accepted());
    ScriptTaskAcceptedAppFrame refused;
    assert(take_script_task_app_frame(wrong_supervisor, wrong_session, codec, refused) ==
           ScriptTaskAppFrameTakeStatus::TextCatalogRejected);

    auto rejected = decoded;
    rejected.shaped_text[0].packet.assign("JFTP1", 5);
    std::vector<std::uint8_t> unchanged{0x5a};
    assert(encode_script_task_app_frame(rejected, codec, unchanged) != ScriptTaskAppFrameCodecStatus::Accepted);
    assert(unchanged == std::vector<std::uint8_t>{0x5a});
    auto wrong_packet_version = decoded;
    wrong_packet_version.shaped_text[0].packet[4] = 1;
    assert(encode_script_task_app_frame(wrong_packet_version, codec, unchanged) ==
           ScriptTaskAppFrameCodecStatus::InvalidValue);
    auto wrong_frame_version = wire;
    wrong_frame_version[4] = kRuntimeShapedFrameVersion == 12 ? 11 : 12;
    ScriptTaskAppFrame preserved;
    preserved.viewport = {7,8,9,10};
    assert(decode_script_task_app_frame(wrong_frame_version, codec, preserved) ==
           ScriptTaskAppFrameCodecStatus::UnsupportedVersion);
    assert(preserved.viewport.x == 7 && preserved.viewport.height == 10);
    for (std::size_t size = 0; size < wire.size(); ++size) {
        const std::vector<std::uint8_t> truncated(wire.begin(), wire.begin()+size);
        assert(decode_script_task_app_frame(truncated, codec, preserved) !=
               ScriptTaskAppFrameCodecStatus::Accepted);
        assert(preserved.viewport.x == 7 && preserved.viewport.height == 10);
    }
    auto invalid_background = decoded;
    invalid_background.display_list[0].rect.width = -1;
    assert(!retained_renderer.render_into(invalid_background, sentinel, clear));
    assert(same_pixels(sentinel, before));
    auto legacy_codec = codec;
    legacy_codec.version = 10;
    assert(encode_script_task_app_frame(decoded, legacy_codec, unchanged) ==
           (JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API ? ScriptTaskAppFrameCodecStatus::UnsupportedVersion :
            ScriptTaskAppFrameCodecStatus::UnsupportedTextFeature));
    auto packet_limited = codec;
    packet_limited.max_text_bytes = decoded.shaped_text[0].packet.size() - 1;
    assert(encode_script_task_app_frame(decoded, packet_limited, unchanged) ==
           ScriptTaskAppFrameCodecStatus::TooManyTextBytes);
    assert(unchanged == std::vector<std::uint8_t>{0x5a});
}
#if JELLYFRAME_RUNTIME_SHAPED_PARAGRAPH_API
void abi4_v11_multiline_capture_damage_and_refusal() {
    using Status = ScriptTaskAppFrameCodecStatus;
    const auto same_pixels = [](const FrameBuffer& a, const FrameBuffer& b) {
        return a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size() &&
            std::equal(a.pixels.begin(), a.pixels.end(), b.pixels.begin(), [](Color x, Color y) {
                return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
            });
    };
    const Color clear{245,245,245,255}, ink{30,60,90,190};
    const Rect viewport{0,0,64,96};
    constexpr auto budget = 32U * 1024U * 1024U;
    auto owner = runtime_test_shaped_text::create_binding(
        runtime_test_shaped_text::Assets("regular", 0x1122334455667788ULL));
    auto work = std::uint64_t(budget);
    runtime_shaped::Metrics single;
    std::string packet;
    assert(owner->prepare(16500, "A", work, packet, single) == runtime_shaped::Status::ok);
    runtime_shaped::ParagraphOptions paragraph_options;
    paragraph_options.size_q1000 = 16500;
    paragraph_options.width_q1000 = single.advance_x;
    paragraph_options.line_height_q1000 = 23001;
    runtime_shaped::Paragraph paragraph;
    work = budget;
    assert(owner->layout_paragraph("A A A", paragraph_options, {}, work, paragraph) == runtime_shaped::Status::ok);
    assert(paragraph.lines().size() == 3);
    ScriptTaskAppFrame base;
    base.viewport = viewport;
    DisplayCommand background; detail::set_command_type(background, DisplayCommandType::FillRect);
    background.rect = viewport; background.color = clear;
    base.display_list.push_back(background);
    ScriptTaskFrameGroup root;
    root.type = LayerType::Root; root.bounds = viewport; root.command_count = 1;
    base.groups.push_back(root);
    ScriptTaskAppFrameCodecOptions limits;
    limits.version = kRuntimeShapedFrameVersion; limits.max_commands = 4; limits.max_text_bytes = 16384;
    limits.max_payload_bytes = 32768; limits.max_groups = 1;
    auto frame = base;
    assert(append_script_task_shaped_paragraph(frame, 1, paragraph, 3250, 2125,
        viewport, ink, limits) == Status::Accepted);
    assert(frame.shaped_text.size() == 3);
    assert(frame.shaped_text.back().baseline_y_q1000 == 2125 + paragraph.lines().back().baseline_y_q1000);
    std::vector<std::uint8_t> wire;
    assert(encode_script_task_app_frame(frame, limits, wire) == Status::Accepted);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(wire, limits, decoded) == Status::Accepted);
    assert(decoded.shaped_text.size() == 3 && decoded.shaped_text.back().packet == paragraph.lines().back().packet);
    ScriptTaskFrameRendererOptions renderer_options;
    renderer_options.shaped_text_binding = owner;
    ScriptTaskFrameRenderer renderer(renderer_options);
    FrameBuffer image(64,96,clear), reference(64,96,clear);
    assert(renderer.render_into(decoded, image, clear));
    // Independent full-clip paint detects a too-small projected ink rectangle.
    for (const auto& line : paragraph.lines()) {
        work = budget;
        assert(runtime_shaped::paint(paragraph.binding(), line.packet, reference, 3250,
            2125 + line.baseline_y_q1000, viewport, ink, {}, work) == runtime_shaped::Status::ok);
    }
    assert(same_pixels(image, reference));

    runtime_test_fonts::Fonts fonts;
    ScriptTaskSupervisorOptions supervisor_options;
    supervisor_options.frame_mailbox = {2,256};
    supervisor_options.frame_leases = {2,32768,65536};
    ScriptTaskSupervisor supervisor(supervisor_options);
    const auto session = supervisor.begin(803, fonts.catalog, {}, owner);
    assert(session.valid() && supervisor.publish_frame(session, wire).accepted());
    ScriptTaskAcceptedAppFrame accepted;
    assert(take_script_task_app_frame(supervisor, session, limits, accepted) == ScriptTaskAppFrameTakeStatus::Accepted);
    assert(accepted.valid() && accepted.frame().shaped_text.size() == 3 && accepted.shaped_text_binding() == owner);
    supervisor.begin_teardown(session);
    FrameBuffer accepted_image(64,96,clear);
    assert(renderer.render_into(accepted, accepted_image, clear) && same_pixels(accepted_image, image));

    // Owner limits below the default are respected without initializing an
    // oversized ledger that Core would reject before reading any packet.
    runtime_test_shaped_text::Assets smaller_assets("regular", 0x1122334455667788ULL);
    RuntimeShapedTextBinding::Handle smaller_owner;
    RuntimeShapedTextLimits smaller_limits; smaller_limits.core.work = budget / 2;
    assert(RuntimeShapedTextBinding::create(smaller_assets.manifest, smaller_owner, smaller_limits) ==
        RuntimeShapedTextStatus::Ready);
    assert(smaller_owner->work_limit() == budget / 2);
    ScriptTaskSupervisor smaller_supervisor(supervisor_options);
    const auto smaller_session = smaller_supervisor.begin(804, fonts.catalog, {}, smaller_owner);
    assert(smaller_session.valid() && smaller_supervisor.publish_frame(smaller_session, wire).accepted());
    ScriptTaskAcceptedAppFrame smaller_accepted;
    assert(take_script_task_app_frame(smaller_supervisor, smaller_session, limits, smaller_accepted) ==
        ScriptTaskAppFrameTakeStatus::Accepted);
    assert(renderer.render_into(smaller_accepted, accepted_image, clear) && same_pixels(accepted_image, image));

    auto changed = decoded;
    changed.shaped_text[2].color = {180,25,10,200};
    const auto diff = diff_script_task_app_frames(decoded, changed);
    assert(diff.paint_structure_equal && diff.changed_command_count == 1 && diff.has_changed_command_bounds);
    assert(diff.changed_command_bounds.y == changed.shaped_text[2].bounds.y);
    const auto damage = plan_script_task_group_damage(renderer, decoded, changed);
    assert(damage.kind == ScriptTaskFrameDamageKind::Partial && damage.rect.y == diff.changed_command_bounds.y);
    assert(plan_script_task_group_damage(renderer, decoded, decoded).kind == ScriptTaskFrameDamageKind::Unchanged);
    auto partial = image;
    assert(renderer.render_into(changed, partial, clear, &damage.rect, 1));
    FrameBuffer full(64,96,clear);
    assert(renderer.render_into(changed, full, clear) && same_pixels(partial, full));
    ScriptTaskFrameRetainedReplay replay({true,64*96,64*96});
    assert(replay.observe_presented(decoded, image, clear, 11));
    FrameBuffer retained;
    ScriptTaskFrameRetainedReplayStatus replay_status;
    assert(replay.render_into(renderer, changed, retained, clear, 11, &replay_status));
    assert(replay_status == ScriptTaskFrameRetainedReplayStatus::Replayed && same_pixels(retained, full));

    // Records at the same insertion point retain their order even when ink overlaps.
    auto overlap = decoded;
    for (auto& item : overlap.shaped_text) {
        item.baseline_y_q1000 = decoded.shaped_text[0].baseline_y_q1000;
        item.bounds = decoded.shaped_text[0].bounds;
    }
    overlap.shaped_text[1].color = {200,15,20,120};
    FrameBuffer ordered(64,96,clear), ordered_reference(64,96,clear);
    assert(renderer.render_into(overlap, ordered, clear));
    for (const auto& item : overlap.shaped_text) {
        work = budget;
        assert(runtime_shaped::paint(owner->core_binding(), item.packet, ordered_reference,
            item.baseline_x_q1000, item.baseline_y_q1000, viewport, item.color, {}, work) == runtime_shaped::Status::ok);
    }
    assert(same_pixels(ordered, ordered_reference));

    auto interleaved = overlap;
    DisplayCommand overlay; detail::set_command_type(overlay, DisplayCommandType::FillRect);
    overlay.rect = viewport; overlay.color = {20,180,40,80};
    interleaved.display_list.push_back(overlay);
    interleaved.groups[0].command_count = 2;
    interleaved.shaped_text.back().after_command_count = 2;
    FrameBuffer interleaved_image(64,96,clear), interleaved_reference(64,96,clear);
    assert(renderer.render_into(interleaved, interleaved_image, clear));
    for (std::size_t i = 0; i < interleaved.shaped_text.size(); ++i) {
        if (i == 2) SoftwareRasterizer{}.rasterize(overlay, interleaved_reference, viewport);
        const auto& item = interleaved.shaped_text[i];
        work = budget;
        assert(runtime_shaped::paint(owner->core_binding(), item.packet, interleaved_reference,
            item.baseline_x_q1000, item.baseline_y_q1000, viewport, item.color, {}, work) == runtime_shaped::Status::ok);
    }
    assert(same_pixels(interleaved_image, interleaved_reference));
    auto negative = base;
    assert(append_script_task_shaped_paragraph(negative, 1, paragraph, -1250, -375,
        viewport, ink, limits) == Status::Accepted);
    FrameBuffer negative_image(64,96,clear), negative_reference(64,96,clear);
    assert(renderer.render_into(negative, negative_image, clear));
    for (const auto& line : paragraph.lines()) {
        work = budget;
        assert(runtime_shaped::paint(owner->core_binding(), line.packet, negative_reference,
            -1250, -375 + line.baseline_y_q1000, viewport, ink, {}, work) == runtime_shaped::Status::ok);
    }
    assert(same_pixels(negative_image, negative_reference));

    // Last-record corruption and cumulative work exhaustion leave target intact.
    auto corrupt = decoded;
    corrupt.shaped_text.back().packet.back() ^= 1;
    auto sentinel = image;
    assert(!renderer.render_into(corrupt, sentinel, clear) && same_pixels(sentinel, image));
    work = budget;
    for (const auto& item : decoded.shaped_text)
        assert(owner->validate_packet(item.packet, work) == runtime_shaped::Status::ok);
    renderer_options.shaped_text_limits.core.work = budget - work + 1;
    ScriptTaskFrameRenderer limited_renderer(renderer_options);
    assert(!limited_renderer.render_into(decoded, sentinel, clear) && same_pixels(sentinel, image));
    renderer_options.shaped_text_limits.core.work = (budget - work) / 3 + 1;
    ScriptTaskFrameRenderer validation_limited_renderer(renderer_options);
    assert(!validation_limited_renderer.render_into(decoded, sentinel, clear) && same_pixels(sentinel, image));

    // Whole-frame budget includes every final packet and regular command.
    auto refused = base;
    auto too_small = limits; too_small.max_commands = 3;
    assert(append_script_task_shaped_paragraph(refused, 1, paragraph, 0, 0, viewport, ink, too_small) == Status::TooManyCommands);
    assert(refused.shaped_text.empty() && refused.display_list.size() == 1);
    too_small = limits; too_small.max_text_bytes = packet.size();
    assert(append_script_task_shaped_paragraph(refused, 1, paragraph, 0, 0, viewport, ink, too_small) == Status::TooManyTextBytes);
    assert(refused.shaped_text.empty());
    assert(append_script_task_shaped_paragraph(refused, 1, paragraph, 0, INT32_MAX, viewport, ink, limits) == Status::InvalidValue);
    assert(refused.shaped_text.empty());
    auto unsorted = decoded; unsorted.shaped_text[2].after_command_count = 0;
    std::vector<std::uint8_t> unchanged{0x5a};
    assert(encode_script_task_app_frame(unsorted, limits, unchanged) == Status::InvalidValue);
    assert(unchanged == std::vector<std::uint8_t>{0x5a});
    assert(append_script_task_shaped_text(decoded, 0, 0, 0, viewport, viewport, ink, packet) == Status::InvalidValue);
    auto oversized_wire = wire;
    detail::canonical::u32(oversized_wire, 40, kScriptTaskMaxShapedTextRecords + 1);
    auto generous = limits; generous.max_commands = 1000;
    assert(decode_script_task_app_frame(oversized_wire, generous, refused) == Status::TooManyCommands);
    assert(refused.shaped_text.empty());
    auto truncated = wire; truncated.pop_back();
    assert(decode_script_task_app_frame(truncated, limits, refused) != Status::Accepted && refused.shaped_text.empty());
    auto byte_limited = limits;
    byte_limited.max_text_bytes = 0;
    for (const auto& item : decoded.shaped_text) byte_limited.max_text_bytes += item.packet.size();
    --byte_limited.max_text_bytes;
    assert(encode_script_task_app_frame(decoded, byte_limited, unchanged) == Status::TooManyTextBytes);
    assert(decode_script_task_app_frame(wire, byte_limited, refused) == Status::TooManyTextBytes);
    assert(refused.shaped_text.empty() && unchanged == std::vector<std::uint8_t>{0x5a});
    auto capped = base;
    capped.shaped_text.assign(kScriptTaskMaxShapedTextRecords, decoded.shaped_text.front());
    auto cap_limits = limits;
    cap_limits.max_commands = kScriptTaskMaxShapedTextRecords + 1;
    cap_limits.max_text_bytes = 1024*1024; cap_limits.max_payload_bytes = 2*1024*1024;
    assert(encode_script_task_app_frame(capped, cap_limits, unchanged) == Status::Accepted);
    assert(decode_script_task_app_frame(unchanged, cap_limits, refused) == Status::Accepted);
    assert(refused.shaped_text.size() == kScriptTaskMaxShapedTextRecords);
    assert(append_script_task_shaped_text(capped, 1, 0, 0, viewport, viewport, ink, packet) == Status::TooManyCommands);

    // Removing a line invalidates the paint skeleton; damage must not be unchanged.
    auto removed = decoded; removed.shaped_text.pop_back();
    assert(diff_script_task_app_frames(decoded, removed).changed_command_count == 1);
    assert(plan_script_task_group_damage(renderer, decoded, removed).kind == ScriptTaskFrameDamageKind::Full);
    owner.reset(); paragraph = {};
    assert(renderer.render_into(decoded, sentinel, clear) && same_pixels(sentinel, image));
}
#endif
#endif

ScriptTaskAppFrame composition_frame(const LayerNode& root, Rect viewport) {
    if (!script_task_app_frame_codec_supports_version(6))
        return make_script_task_app_frame(root, viewport, {}, true);
    ScriptTaskAppFrameCodecOptions options;
    options.version = 6;
    options.max_commands = 128;
    options.max_text_bytes = 16384;
    options.max_payload_bytes = 65536;
    options.max_groups = 128;
    ScriptTaskAppFrame captured, decoded;
    assert(make_script_task_group_frame(root, viewport, options, captured) == ScriptTaskAppFrameCodecStatus::Accepted);
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(captured, options, bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(decode_script_task_app_frame(bytes, options, decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    return decoded;
}

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
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
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

void renderer_applies_value_frame_rotation_with_a_rounded_clip() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 32, 32};
    frame.clips = {{{4, 4, 24, 24}, 8, kScriptTaskNoParentClip}};
    DisplayCommand hand;
    jellyframe::detail::set_command_type(hand, DisplayCommandType::FillRect);
    hand.rect = {15, 5, 2, 12};
    hand.color = {240, 80, 90, 255};
    jellyframe::detail::command_border_radius(hand) = 1;
    hand.transform.enabled = true;
    hand.transform.xx_1024 = 0;
    hand.transform.xy_1024 = -1024;
    hand.transform.yx_1024 = 1024;
    hand.transform.yy_1024 = 0;
    hand.transform.tx_1024 = 32 * 1024;
    hand.transform.ty_1024 = 0;
    frame.display_list.push_back(hand);
    frame.display_clip_indices.push_back(0);

    ScriptTaskFrameRenderer renderer;
    const FrameBuffer output = renderer.render(frame, {255, 255, 255, 255});
    assert(output.pixel(22, 16).r > 220 && output.pixel(22, 16).g < 100);
    assert(output.pixel(15, 8).r == 255 && output.pixel(15, 8).g == 255);
    assert(output.pixel(5, 5).r == 255 && output.pixel(5, 5).g == 255);
}

void renderer_matches_layer_compositor_for_right_angle_value_rotation() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = {0, 0, 32, 32};
    LayerNodePtr clipped(new LayerNode(), LayerNodeDeleter{});
    clipped->type = LayerType::Clip;
    clipped->has_clip = true;
    clipped->clip_rect = {4, 4, 24, 24};
    clipped->clip_border_radius = 8;
    LayerNodePtr hand(new LayerNode(), LayerNodeDeleter{});
    hand->type = LayerType::Composited;
    hand->bounds = {15, 5, 2, 12};
    hand->transform.rotate_degrees = 90.0F;
    hand->transform_origin_x_percent = 50;
    hand->transform_origin_y_percent = 100;
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = hand->bounds;
    fill.color = {240, 80, 90, 255};
    jellyframe::detail::command_border_radius(fill) = 1;
    hand->display_list.push_back(fill);
    clipped->children.push_back(std::move(hand));
    root.children.push_back(std::move(clipped));

    const FrameBuffer expected = SoftwareCompositor().render(root, 32, 32, {255, 255, 255, 255});
    const ScriptTaskAppFrame frame = composition_frame(root, {0, 0, 32, 32});
    const FrameBuffer actual = ScriptTaskFrameRenderer().render(frame, {255, 255, 255, 255});
    assert(actual.pixels.size() == expected.pixels.size());
    for (std::size_t index = 0; index < actual.pixels.size(); ++index) {
        assert(std::abs(static_cast<int>(actual.pixels[index].r) - static_cast<int>(expected.pixels[index].r)) <= 1);
        assert(std::abs(static_cast<int>(actual.pixels[index].g) - static_cast<int>(expected.pixels[index].g)) <= 1);
        assert(std::abs(static_cast<int>(actual.pixels[index].b) - static_cast<int>(expected.pixels[index].b)) <= 1);
        assert(actual.pixels[index].a == expected.pixels[index].a);
    }
}

void renderer_applies_ancestor_clip_only_in_destination_space_for_transforms() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = {0, 0, 32, 32};
    LayerNodePtr clipped(new LayerNode(), LayerNodeDeleter{});
    clipped->type = LayerType::Clip;
    clipped->has_clip = true;
    clipped->clip_rect = {8, 8, 16, 16};
    LayerNodePtr rotated(new LayerNode(), LayerNodeDeleter{});
    rotated->type = LayerType::Composited;
    rotated->bounds = {8, 2, 16, 12};
    rotated->transform.rotate_degrees = 90.0F;
    rotated->transform_origin_x_percent = 50;
    rotated->transform_origin_y_percent = 50;
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = rotated->bounds;
    fill.color = {240, 80, 90, 255};
    rotated->display_list.push_back(fill);
    clipped->children.push_back(std::move(rotated));
    root.children.push_back(std::move(clipped));

    const FrameBuffer expected = SoftwareCompositor().render(root, 32, 32, {255, 255, 255, 255});
    const ScriptTaskAppFrame frame = composition_frame(root, {0, 0, 32, 32});
    const FrameBuffer actual = ScriptTaskFrameRenderer().render(frame, {255, 255, 255, 255});
    assert(actual.pixels.size() == expected.pixels.size());
    for (std::size_t index = 0; index < actual.pixels.size(); ++index) {
        assert(std::abs(static_cast<int>(actual.pixels[index].r) - static_cast<int>(expected.pixels[index].r)) <= 1);
        assert(std::abs(static_cast<int>(actual.pixels[index].g) - static_cast<int>(expected.pixels[index].g)) <= 1);
        assert(std::abs(static_cast<int>(actual.pixels[index].b) - static_cast<int>(expected.pixels[index].b)) <= 1);
        assert(actual.pixels[index].a == expected.pixels[index].a);
    }
}

void renderer_accepts_degenerate_value_frame_transform_as_empty_paint() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 16, 16};
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = {2, 2, 12, 12};
    fill.color = {240, 80, 90, 255};
    fill.transform.enabled = true;
    fill.transform.xx_1024 = 0;
    fill.transform.xy_1024 = 0;
    fill.transform.yx_1024 = 0;
    fill.transform.yy_1024 = 1024;
    frame.display_list.push_back(fill);

    ScriptTaskFrameRenderStatus status = ScriptTaskFrameRenderStatus::InvalidFrame;
    const FrameBuffer output = ScriptTaskFrameRenderer().render(frame, {255, 255, 255, 255}, &status);
    assert(status == ScriptTaskFrameRenderStatus::Accepted);
    for (const Color pixel : output.pixels) {
        assert(pixel.r == 255 && pixel.g == 255 && pixel.b == 255 && pixel.a == 255);
    }
}

void renderer_matches_transformed_layer_own_clip() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = {0, 0, 32, 32};
    LayerNodePtr rotated(new LayerNode(), LayerNodeDeleter{});
    rotated->type = LayerType::Composited;
    rotated->bounds = {8, 2, 16, 12};
    rotated->has_clip = true;
    rotated->clip_rect = {8, 8, 16, 6};
    rotated->transform.rotate_degrees = 90.0F;
    rotated->transform_origin_x_percent = 50;
    rotated->transform_origin_y_percent = 50;
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = rotated->bounds;
    fill.color = {240, 80, 90, 255};
    rotated->display_list.push_back(fill);
    root.children.push_back(std::move(rotated));

    const FrameBuffer expected = SoftwareCompositor().render(root, 32, 32, {255, 255, 255, 255});
    const ScriptTaskAppFrame frame = composition_frame(root, {0, 0, 32, 32});
    const FrameBuffer actual = ScriptTaskFrameRenderer().render(frame, {255, 255, 255, 255});
    assert(actual.pixels.size() == expected.pixels.size());
    for (std::size_t index = 0; index < actual.pixels.size(); ++index) {
        assert(std::abs(static_cast<int>(actual.pixels[index].r) - static_cast<int>(expected.pixels[index].r)) <= 1);
        assert(std::abs(static_cast<int>(actual.pixels[index].g) - static_cast<int>(expected.pixels[index].g)) <= 1);
        assert(std::abs(static_cast<int>(actual.pixels[index].b) - static_cast<int>(expected.pixels[index].b)) <= 1);
        assert(actual.pixels[index].a == expected.pixels[index].a);
    }
}

void renderer_matches_nested_source_clips_for_transformed_child_content() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = {0, 0, 40, 40};

    LayerNodePtr rotated(new LayerNode(), LayerNodeDeleter{});
    rotated->type = LayerType::Composited;
    rotated->bounds = {6, 4, 24, 22};
    rotated->has_clip = true;
    rotated->clip_rect = {11, 9, 14, 11};
    rotated->transform.rotate_degrees = 90.0F;
    rotated->transform_origin_x_percent = 50;
    rotated->transform_origin_y_percent = 50;

    LayerNodePtr child(new LayerNode(), LayerNodeDeleter{});
    child->type = LayerType::Paint;
    child->bounds = {2, 1, 32, 28};
    child->has_clip = true;
    child->clip_rect = {8, 7, 16, 15};
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = child->bounds;
    fill.color = {240, 80, 90, 255};
    child->display_list.push_back(fill);
    rotated->children.push_back(std::move(child));
    root.children.push_back(std::move(rotated));

    const FrameBuffer expected = SoftwareCompositor().render(root, 40, 40, {255, 255, 255, 255});
    const ScriptTaskAppFrame frame = composition_frame(root, {0, 0, 40, 40});
    const FrameBuffer actual = ScriptTaskFrameRenderer().render(frame, {255, 255, 255, 255});
    assert(actual.pixels.size() == expected.pixels.size());
    for (std::size_t index = 0; index < actual.pixels.size(); ++index) {
        assert(std::abs(static_cast<int>(actual.pixels[index].r) - static_cast<int>(expected.pixels[index].r)) <= 1);
        assert(std::abs(static_cast<int>(actual.pixels[index].g) - static_cast<int>(expected.pixels[index].g)) <= 1);
        assert(std::abs(static_cast<int>(actual.pixels[index].b) - static_cast<int>(expected.pixels[index].b)) <= 1);
        assert(actual.pixels[index].a == expected.pixels[index].a);
    }
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
    jellyframe::detail::set_command_type(blue, DisplayCommandType::FillRect);
    blue.rect = {0, 0, 40, 40};
    blue.color = {20, 120, 240, 160};
    DisplayCommand red;
    jellyframe::detail::set_command_type(red, DisplayCommandType::FillRect);
    red.rect = {12, 12, 20, 20};
    red.color = {240, 80, 70, 128};
    clipped->display_list.push_back(blue);
    clipped->display_list.push_back(red);
    root.children.push_back(std::move(clipped));

    const FrameBuffer expected = SoftwareCompositor().render(root, 40, 40, {255, 255, 255, 255});
    const ScriptTaskAppFrame frame = composition_frame(root, {0, 0, 40, 40});
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

void renderer_bounds_large_dirty_input_before_normalization() {
    ScriptTaskFrameRenderer renderer;
    ScriptTaskAppFrame frame = rounded_frame();
    FrameBuffer output(40, 40, {90, 90, 90, 255});
    std::vector<Rect> dirty;
    dirty.reserve(129);
    for (int index = 0; index < 129; ++index) {
        dirty.push_back(Rect{index, 0, 1, 1});
    }
    ScriptTaskFrameRenderStatus status = ScriptTaskFrameRenderStatus::InvalidFrame;
    assert(renderer.render_into(frame,
                                output,
                                {255, 255, 255, 255},
                                dirty.data(),
                                dirty.size(),
                                nullptr,
                                &status));
    assert(status == ScriptTaskFrameRenderStatus::Accepted);
    assert(output.pixel(20, 20).b > 200);
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

    if (detail::kScriptTaskSamplingTransportAvailable) {
        current = previous;
        detail::set_command_sampling_rect(current.display_list[0], {2, 3, 36, 34});
        const ScriptTaskFrameDiff changed_sampling_report = diff_script_task_app_frames(previous, current);
        assert(changed_sampling_report.unchanged_command_count == 0 &&
               changed_sampling_report.changed_command_count == 1 && changed_sampling_report.has_changed_command_bounds);
    }

    if constexpr (detail::has_background_clip_transport<DisplayCommand>()) {
        current = previous;
        detail::set_command_type(current.display_list[0], DisplayCommandType::Image);
        previous.display_list[0] = current.display_list[0];
        detail::set_command_background_paint_clip(previous.display_list[0], {0, 0, 8, 8});
        current.display_list[0] = previous.display_list[0];
        detail::set_command_background_paint_clip(current.display_list[0], {1, 0, 8, 8});
        const ScriptTaskFrameDiff changed_background_clip = diff_script_task_app_frames(previous, current);
        assert(changed_background_clip.changed_command_count == 1 &&
               changed_background_clip.unchanged_command_count == 0);
    }

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
    jellyframe::detail::set_command_type(badge, DisplayCommandType::FillRect);
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
    jellyframe::detail::set_command_type(background, DisplayCommandType::FillRect);
    background.rect = {0, 0, 40, 24};
    background.color = {24, 40, 68, 255};
    frame.display_list.push_back(background);
    DisplayCommand card;
    jellyframe::detail::set_command_type(card, DisplayCommandType::LinearGradient);
    card.rect = {4, 3, 32, 18};
    card.color = {60, 180, 220, 255};
    card.color2 = {40, 100, 180, 255};
    jellyframe::detail::command_border_radius(card) = 6;
    frame.display_list.push_back(card);
    DisplayCommand overlay;
    jellyframe::detail::set_command_type(overlay, DisplayCommandType::FillRect);
    overlay.rect = {12, 8, 12, 8};
    overlay.color = {250, 90, 70, 128};
    jellyframe::detail::command_border_radius(overlay) = 3;
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

void retained_replay_matches_canonical_output_for_background_clip_mutation() {
    if (!detail::has_background_clip_transport<DisplayCommand>()) return;
#if defined(JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED) && !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif

    ScriptTaskFrameRenderer renderer;
    const Color background{255, 255, 255, 255};
    ScriptTaskAppFrame previous = replay_candidate_frame();
    detail::set_command_background_paint_clip(previous.display_list[1], {0, 0, 32, 18});
    const FrameBuffer previous_image = renderer.render(previous, background);

    ScriptTaskAppFrame current = previous;
    detail::set_command_background_paint_clip(current.display_list[1], {8, 4, 16, 10});
    const FrameBuffer expected = renderer.render(current, background);
    assert(!equal_framebuffer_pixels(previous_image, expected));

    ScriptTaskFrameRetainedReplayOptions options;
    options.enabled = true;
    options.max_retained_pixels = static_cast<std::size_t>(previous.viewport.width) *
        static_cast<std::size_t>(previous.viewport.height);
    options.max_replay_pixels = options.max_retained_pixels;
    ScriptTaskFrameRetainedReplay replay(options);
    assert(replay.observe_presented(previous, previous_image, background, 13));
    FrameBuffer actual;
    ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled;
    assert(replay.render_into(renderer, current, actual, background, 13, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::Replayed);
    assert(equal_framebuffer_pixels(actual, expected));
    assert(replay.observe_presented(current, actual, background, 13));

    const auto& statistics = replay.statistics();
    assert(statistics.candidates == 1 && statistics.replays == 1 &&
           statistics.pixel_mismatch_fallbacks == 0 && statistics.pixel_mismatch_pixels == 0);
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
    assert(replay.statistics().full_frame_without_previous == 1 &&
           replay.statistics().full_frame_replay_ineligible == 1 && replay.statistics().candidates == 0);

    replay.reset();
    options.enabled = false;
    ScriptTaskFrameRetainedReplay disabled(options);
    assert(disabled.render_into(renderer, current, output, background, 7, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled);
    assert(disabled.statistics().full_frame_replay_disabled == 1);
}

void retained_replay_rejects_unbounded_host_painters() {
    DisplayCommand text;
    jellyframe::detail::set_command_type(text, DisplayCommandType::Text);
    text.rect = {1, 2, 12, 8};
    Rect bounds;
    assert(!script_task_frame_command_visual_bounds(text, bounds));
    assert(bounds.width == 0 && bounds.height == 0);

    DisplayCommand image;
    jellyframe::detail::set_command_type(image, DisplayCommandType::Image);
    image.rect = {1, 2, 12, 8};
    assert(!script_task_frame_command_visual_bounds(image, bounds));

    DisplayCommand shadow;
    jellyframe::detail::set_command_type(shadow, DisplayCommandType::BoxShadow);
    shadow.rect = {1, 2, 12, 8};
    assert(script_task_frame_command_visual_bounds(shadow, bounds));
    assert(bounds.x == 1 && bounds.y == 2 && bounds.width == 12 && bounds.height == 8);
}

ScriptTaskAppFrame text_replay_frame() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 24, 12};
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = {0, 0, 24, 12};
    fill.color = {20, 30, 40, 255};
    frame.display_list.push_back(fill);
    DisplayCommand text;
    jellyframe::detail::set_command_type(text, DisplayCommandType::Text);
    text.rect = {7, 4, 8, 4};
    text.color = {80, 220, 130, 255};
    text.text = "1";
    frame.display_list.push_back(text);
    return frame;
}

void abi4_renderer_refuses_legacy_text_without_writing() {
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API
    ScriptTaskFrameRenderer renderer;
    auto frame = text_replay_frame();
    FrameBuffer target(frame.viewport.width, frame.viewport.height, {1, 2, 3, 4});
    const auto before = target;
    ScriptTaskFrameRenderStatus status = ScriptTaskFrameRenderStatus::Accepted;
    assert(!renderer.render_into(frame, target, {255, 255, 255, 255}, nullptr, 0, nullptr, &status));
    assert(status == ScriptTaskFrameRenderStatus::InvalidFrame);
    assert(equal_framebuffer_pixels(target, before));
#endif
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

template<class Request>
bool bounded_marker_image_request_painter(FrameBuffer& target, const Request& request, void* context) {
    return bounded_marker_image_painter(target, request.destination, request.image_handle,
        request.object_fit, request.object_position, request.image_rendering, context);
}
template<class Painter> Painter make_bounded_marker_image_painter() {
    Painter painter;
    if constexpr (std::is_convertible_v<decltype(&bounded_marker_image_painter), decltype(painter.paint)>)
        painter.paint = bounded_marker_image_painter;
    else painter.paint = bounded_marker_image_request_painter;
    painter.writes_only_within_rect = true;
    return painter;
}

void retained_replay_accepts_explicitly_bounded_image_painter() {
    auto bounded = make_bounded_marker_image_painter<ImagePainter>();
    ScriptTaskFrameRenderer renderer(bounded);
    ScriptTaskFrameRetainedReplayOptions options;
    options.enabled = true;
    options.max_retained_pixels = 24 * 12;
    options.max_replay_pixels = 24 * 12;
    ScriptTaskFrameRetainedReplay replay(options);
    const Color background{255, 255, 255, 255};
    ScriptTaskAppFrame previous = text_replay_frame();
    jellyframe::detail::set_command_type(previous.display_list[1], DisplayCommandType::Image);
    jellyframe::detail::command_image_handle(previous.display_list[1]) = 7;
    previous.display_list[1].text.clear();
    const FrameBuffer previous_image = renderer.render(previous, background);
    assert(replay.observe_presented(previous, previous_image, background, 3));

    ScriptTaskAppFrame current = previous;
    jellyframe::detail::command_object_position(current.display_list[1]).x_percent = 75;
    FrameBuffer actual;
    ScriptTaskFrameRetainedReplayStatus status = ScriptTaskFrameRetainedReplayStatus::FullFrameDisabled;
    assert(replay.render_into(renderer, current, actual, background, 3, &status));
    assert(status == ScriptTaskFrameRetainedReplayStatus::Replayed);
    assert(equal_framebuffer_pixels(actual, renderer.render(current, background)));
}

void value_frame_renderer_rejects_unsupported_sampling_without_writing() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 16, 12};
    DisplayCommand image;
    jellyframe::detail::set_command_type(image, DisplayCommandType::Image);
    image.rect = {2, 2, 8, 6};
    detail::set_command_sampling_rect(image, {1, 2, 12, 6});
    image.text.assign("JFFS\0fractional", 15);
    DisplayCommand background;
    background.rect = frame.viewport;
    background.color = {255, 0, 0, 255};
    frame.display_list = {background, image};

    FrameBuffer target(16, 12, {12, 24, 36, 255});
    const FrameBuffer before = target;
    ScriptTaskFrameRenderer renderer;
    ScriptTaskFrameRenderStatus status = ScriptTaskFrameRenderStatus::Accepted;
    for (const auto type : {DisplayCommandType::Image, DisplayCommandType::LinearGradient,
                           DisplayCommandType::ConicGradient, DisplayCommandType::RadialGradient}) {
        jellyframe::detail::set_command_type(frame.display_list.back(), type);
        assert(!renderer.render_into(frame, target, {255, 255, 255, 255}, nullptr, 0, nullptr, &status));
        assert(status == ScriptTaskFrameRenderStatus::UnsupportedSamplingFeature);
        assert(equal_framebuffer_pixels(target, before));
    }
    if (script_task_app_frame_codec_supports_version(6)) {
        ScriptTaskFrameGroup group;
        group.bounds = frame.viewport;
        group.command_count = static_cast<std::uint32_t>(frame.display_list.size());
        frame.groups.push_back(group);
        assert(!renderer.render_into(frame, target, {255, 255, 255, 255}, nullptr, 0, nullptr, &status));
        assert(status == ScriptTaskFrameRenderStatus::UnsupportedSamplingFeature);
        assert(equal_framebuffer_pixels(target, before));
        frame.groups.clear();
    }
    if (detail::kScriptTaskSamplingTransportAvailable) {
        frame.display_list.back().text.clear();
        assert(!renderer.render_into(frame, target, {255, 255, 255, 255}, nullptr, 0, nullptr, &status));
        assert(status == ScriptTaskFrameRenderStatus::UnsupportedSamplingFeature);
        assert(equal_framebuffer_pixels(target, before));
    }
}

ScriptTaskAppFrame retained_replay_stress_frame() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 64, 48};

    DisplayCommand background;
    jellyframe::detail::set_command_type(background, DisplayCommandType::FillRect);
    background.rect = {0, 0, 64, 48};
    background.color = {16, 24, 38, 255};
    frame.display_list.push_back(background);

    DisplayCommand shadow;
    jellyframe::detail::set_command_type(shadow, DisplayCommandType::BoxShadow);
    shadow.rect = {6, 5, 52, 38};
    shadow.color = {0, 0, 0, 92};
    jellyframe::detail::command_border_radius(shadow) = 10;
    jellyframe::detail::command_stroke_width(shadow) = 5;
    frame.display_list.push_back(shadow);

    DisplayCommand surface;
    jellyframe::detail::set_command_type(surface, DisplayCommandType::LinearGradient);
    surface.rect = {10, 8, 44, 30};
    surface.color = {58, 184, 214, 255};
    surface.color2 = {52, 84, 182, 255};
    jellyframe::detail::command_gradient_axis(surface) = GradientAxis::DiagonalDownRight;
    jellyframe::detail::command_gradient_stop_percent(surface) = 72;
    jellyframe::detail::command_border_radius(surface) = 8;
    frame.display_list.push_back(surface);

    DisplayCommand accent;
    jellyframe::detail::set_command_type(accent, DisplayCommandType::FillRect);
    accent.rect = {18, 19, 10, 7};
    accent.color = {247, 105, 81, 144};
    jellyframe::detail::command_border_radius(accent) = 3;
    frame.display_list.push_back(accent);

    frame.clips = {{{10, 8, 44, 30}, 8, kScriptTaskNoParentClip}};
    frame.display_clip_indices = {kScriptTaskNoClip, kScriptTaskNoClip, 0, 0};
    return frame;
}

ScriptTaskAppFrame retained_replay_nested_clip_frame() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 64, 48};

    DisplayCommand background;
    jellyframe::detail::set_command_type(background, DisplayCommandType::FillRect);
    background.rect = {0, 0, 64, 48};
    background.color = {17, 25, 40, 255};
    frame.display_list.push_back(background);

    DisplayCommand shadow;
    jellyframe::detail::set_command_type(shadow, DisplayCommandType::BoxShadow);
    shadow.rect = {5, 4, 54, 40};
    shadow.color = {0, 0, 0, 96};
    jellyframe::detail::command_border_radius(shadow) = 10;
    jellyframe::detail::command_stroke_width(shadow) = 5;
    frame.display_list.push_back(shadow);

    DisplayCommand underlay;
    jellyframe::detail::set_command_type(underlay, DisplayCommandType::FillRect);
    underlay.rect = {19, 16, 15, 11};
    underlay.color = {93, 116, 180, 196};
    jellyframe::detail::command_border_radius(underlay) = 3;
    frame.display_list.push_back(underlay);

    DisplayCommand panel;
    jellyframe::detail::set_command_type(panel, DisplayCommandType::LinearGradient);
    panel.rect = {8, 7, 48, 34};
    panel.color = {57, 193, 209, 255};
    panel.color2 = {65, 87, 184, 255};
    jellyframe::detail::command_gradient_axis(panel) = GradientAxis::DiagonalDownRight;
    jellyframe::detail::command_gradient_stop_percent(panel) = 68;
    jellyframe::detail::command_border_radius(panel) = 9;
    frame.display_list.push_back(panel);

    DisplayCommand accent;
    jellyframe::detail::set_command_type(accent, DisplayCommandType::FillRect);
    accent.rect = {19, 17, 12, 8};
    accent.color = {250, 124, 89, 172};
    jellyframe::detail::command_border_radius(accent) = 4;
    frame.display_list.push_back(accent);

    DisplayCommand highlight;
    jellyframe::detail::set_command_type(highlight, DisplayCommandType::StrokeRect);
    highlight.rect = {16, 14, 30, 21};
    highlight.color = {238, 249, 255, 144};
    jellyframe::detail::command_border_radius(highlight) = 7;
    jellyframe::detail::command_stroke_width(highlight) = 2;
    frame.display_list.push_back(highlight);

    frame.clips = {
        {{8, 7, 48, 34}, 9, kScriptTaskNoParentClip},
        {{14, 12, 36, 25}, 7, 0},
    };
    frame.display_clip_indices = {kScriptTaskNoClip, kScriptTaskNoClip, kScriptTaskNoClip, 0, 1, 1};
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

void retained_replay_matches_canonical_output_across_nested_clip_mutations() {
    ScriptTaskFrameRenderer renderer;
    ScriptTaskAppFrame previous = retained_replay_nested_clip_frame();
    const Color background{239, 244, 250, 255};
    ScriptTaskFrameRetainedReplay replay(retained_replay_options_for(previous.viewport));
    const FrameBuffer presented = renderer.render(previous, background);
    assert(replay.observe_presented(previous, presented, background, 11));

    constexpr int kIterations = 96;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        ScriptTaskAppFrame current = previous;
        // This command is intentionally beneath both rounded clip groups.
        // Its local change must still expand replay through each overlapping
        // rounded group before their translucent edges are re-composited.
        DisplayCommand& underlay = current.display_list[2];
        underlay.rect.x = 15 + (iteration % 18);
        underlay.rect.y = 14 + ((iteration * 5) % 14);
        underlay.color = {static_cast<std::uint8_t>(84 + (iteration * 13) % 140),
                          static_cast<std::uint8_t>(99 + (iteration * 17) % 130),
                          static_cast<std::uint8_t>(110 + (iteration * 7) % 120),
                          static_cast<std::uint8_t>(84 + (iteration % 6) * 28)};

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
           statistics.pixel_mismatch_fallbacks == 0 && statistics.replayed_command_groups >= kIterations &&
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
    jellyframe::detail::set_command_type(changed_type.display_list[3], DisplayCommandType::StrokeRect);
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

    ScriptTaskAppFrame removed = base;
    removed.display_list.pop_back();
    removed.display_clip_indices.pop_back();
    verify_fallback(removed,
                    background,
                    11,
                    64 * 48,
                    ScriptTaskFrameRetainedReplayFallbackReason::PaintSkeleton);

    ScriptTaskAppFrame reshaped_viewport = base;
    reshaped_viewport.viewport = {0, 0, 48, 64};
    verify_fallback(reshaped_viewport,
                    background,
                    11,
                    64 * 48,
                    ScriptTaskFrameRetainedReplayFallbackReason::PreviousImageDimensions);

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
            jellyframe::detail::command_gradient_stop_percent(command) = 20 + static_cast<int>(value % 81U);
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
    assert(replay.statistics().full_frame_render_rejected == 1 && replay.statistics().replays == 2 &&
           replay.statistics().pixel_mismatch_fallbacks == 0);
    assert(replay.statistics().ineligible_by_reason[
               static_cast<std::size_t>(ScriptTaskFrameRetainedReplayFallbackReason::InvalidFrame)] == 1);
}

void v6_groups_match_compositor_across_nested_effects_and_dirty_tiles() {
#if JELLYFRAME_SCRIPT_TASK_GROUP_API
    std::size_t compared_pixels = 0;
    for (int scene = 0; scene < 24; ++scene) {
        LayerNode root;
        root.type = LayerType::Root;
        root.bounds = {0, 0, 48, 48};
        root.has_clip = true; root.clip_rect = {2, 3, 42, 39};
        LayerNodePtr outer(new LayerNode(), LayerNodeDeleter{});
        outer->type = LayerType::Composited;
        outer->bounds = {8, 5, 27, 29};
        outer->has_clip = true; outer->clip_rect = {10, 9, 23, 21};
        outer->clip_axes = static_cast<std::uint8_t>(1 + scene % 3);
        outer->opacity = scene % 2 ? 0.375F : 1.0F;
        outer->transform.rotate_degrees = scene % 3 ? 27.125F : 90.0F;
        outer->transform.scale_x = scene % 4 == 0 ? -0.875F : 1.125F;
        outer->transform.scale_y = 0.75F;
        outer->transform.translate_x = 1.25F;
        outer->transform.translate_y = -0.75F;
        outer->transform_origin_x_percent = -15;
        outer->transform_origin_y_percent = 70;
        if (scene % 4 == 1) {
            outer->clip_border_radius = 6;
            outer->clip_ellipse_geometry = std::make_unique<EllipticalCornerRadii>(
                EllipticalCornerRadii{{7, 3, 5, 4}, {3, 6, 4, 7}});
        }
#if JELLYFRAME_RENDER_CORE_AFFINE2D_ENABLED
        if (scene % 4 == 2) outer->affine_transform = std::make_unique<AffineTransform2D>(
            AffineTransform2D{0.875F, -0.125F, 0.25F, 1.125F, 3.125F, -1.25F});
        if (scene == 23) outer->affine_transform = std::make_unique<AffineTransform2D>(
            AffineTransform2D{0, 0, 0, 1, 0, 0});
#endif
        DisplayCommand blue;
        blue.rect = {6, 3, 30, 30}; blue.color = {20, 80, 230, 173};
        outer->display_list.push_back(blue);
        auto red = blue; red.rect = {12, 11, 21, 21}; red.color = {235, 70, 30, 149};
        outer->display_list.push_back(red);
        outer->ellipse_geometry = std::make_unique<CommandEllipseGeometryList>();
        outer->ellipse_geometry->entries.push_back({1, {{5, 4, 3, 7}, {2, 6, 4, 3}}});
        LayerNodePtr child(new LayerNode(), LayerNodeDeleter{});
        child->type = LayerType::Composited;
        child->bounds = {4, 10, 28, 25};
        child->has_clip = true; child->clip_rect = {9, 7, 21, 24};
        child->clip_border_radius = scene % 5 == 0 ? 5 : 0;
        child->opacity = 0.625F;
        child->transform.rotate_degrees = scene % 2 ? -14.375F : 0;
        child->transform.translate_y = 2;
        auto green = red; green.rect = {4, 7, 29, 25}; green.color = {20, 215, 65, 193};
        child->display_list.push_back(green);
        outer->children.push_back(std::move(child));
        root.children.push_back(std::move(outer));
        const Color background{245, 240, 235, 255};
        const auto expected = SoftwareCompositor().render(root, 48, 48, background);
        auto frame = composition_frame(root, {0, 0, 48, 48});
        root.children.clear(); // The frame owns every value and ellipse sidecar.
        assert(frame.groups.size() == 3 && frame.display_list.size() == 3);
        assert(frame.command_ellipses.size() == 1);
        assert(frame.display_list[0].color.a == 173);
        ScriptTaskFrameRenderStatus status;
        const auto actual = ScriptTaskFrameRenderer().render(frame, background, &status);
        assert(status == ScriptTaskFrameRenderStatus::Accepted);
        assert(actual.pixels.size() == expected.pixels.size());
        for (std::size_t i = 0; i < actual.pixels.size(); ++i) {
            assert(actual.pixels[i].r == expected.pixels[i].r && actual.pixels[i].g == expected.pixels[i].g &&
                actual.pixels[i].b == expected.pixels[i].b && actual.pixels[i].a == expected.pixels[i].a);
            ++compared_pixels;
        }
        FrameBuffer tiled(48, 48, {101, 102, 103, 255});
        for (int y = 0; y < 48; y += 8) {
            const Rect dirty{0, y, 48, 8};
            assert(ScriptTaskFrameRenderer().render_into(frame, tiled, background, &dirty, 1));
        }
        for (std::size_t i = 0; i < tiled.pixels.size(); ++i) {
            assert(tiled.pixels[i].r == expected.pixels[i].r && tiled.pixels[i].g == expected.pixels[i].g &&
                tiled.pixels[i].b == expected.pixels[i].b && tiled.pixels[i].a == expected.pixels[i].a);
            ++compared_pixels;
        }
        // Structural rejection precedes any framebuffer clear or painter call.
        auto invalid = frame; invalid.groups.back().parent = 99;
        FrameBuffer sentinel(48, 48, {11, 22, 33, 44});
        assert(!ScriptTaskFrameRenderer().render_into(invalid, sentinel, background));
        for (const auto pixel : sentinel.pixels) assert(pixel.r == 11 && pixel.a == 44);
        invalid = frame;
        invalid.command_ellipses.push_back(invalid.command_ellipses.front());
        assert(!ScriptTaskFrameRenderer().render_into(invalid, sentinel, background));
        for (const auto pixel : sentinel.pixels) assert(pixel.r == 11 && pixel.a == 44);
        assert(!diff_script_task_app_frames(frame, frame).paint_structure_equal);
        if (scene == 0) {
            for (const std::size_t limit : {1U, 255U, 256U, 511U, 512U, 1023U, 2048U, 4096U}) {
                ScriptTaskFrameRendererOptions limited_options;
                limited_options.max_temporary_pixels = limit;
                auto limited_frame = frame;
                const auto limited_image = ScriptTaskFrameRenderer(limited_options).render(limited_frame, background);
                // Budget branches depend on full reference surfaces, not damage tiles.
                FrameBuffer limited_tiled(48, 48, background);
                for (int y = 0; y < 48; y += 8) {
                    const Rect dirty{0, y, 48, 8};
                    assert(ScriptTaskFrameRenderer(limited_options).render_into(limited_frame,
                        limited_tiled, background, &dirty, 1));
                }
                assert(equal_framebuffer_pixels(limited_image, limited_tiled));
            }
            ScriptTaskFrameRendererOptions depth_limit; depth_limit.max_group_depth = 2;
            assert(!ScriptTaskFrameRenderer(depth_limit).render_into(frame, sentinel, background));
            for (const auto pixel : sentinel.pixels) assert(pixel.r == 11 && pixel.a == 44);
        }
    }
    std::cout << "v6 groups: 24 scenes, full/tiled exact RGBA pixels=" << compared_pixels << "\n";
#endif
}

void v6_group_frame_fixes_legacy_source_surface_fringe() {
#if JELLYFRAME_SCRIPT_TASK_GROUP_API
    LayerNode root;
    root.type = LayerType::Root; root.bounds = {0, 0, 32, 32};
    LayerNodePtr rotated(new LayerNode(), LayerNodeDeleter{});
    rotated->type = LayerType::Composited; rotated->bounds = {8, 2, 16, 12};
    rotated->has_clip = true; rotated->clip_rect = {8, 8, 16, 6};
    rotated->transform.rotate_degrees = 90;
    DisplayCommand fill; fill.rect = rotated->bounds; fill.color = {240, 80, 90, 255};
    rotated->display_list.push_back(fill); root.children.push_back(std::move(rotated));
    const Color background{255, 255, 255, 255};
    const auto reference = SoftwareCompositor().render(root, 32, 32, background);
    const auto legacy = ScriptTaskFrameRenderer().render(
        make_script_task_app_frame(root, root.bounds, {}, true), background);
    const auto grouped = ScriptTaskFrameRenderer().render(composition_frame(root, root.bounds), background);
    std::size_t legacy_differences = 0;
    for (std::size_t i = 0; i < reference.pixels.size(); ++i) {
        legacy_differences += legacy.pixels[i].r != reference.pixels[i].r;
        assert(grouped.pixels[i].r == reference.pixels[i].r && grouped.pixels[i].g == reference.pixels[i].g &&
            grouped.pixels[i].b == reference.pixels[i].b && grouped.pixels[i].a == reference.pixels[i].a);
    }
    assert(legacy_differences != 0);
    std::cout << "legacy transformed own-clip differing pixels=" << legacy_differences << "; v6=0\n";
#endif
}

void v6_opacity_is_applied_after_overlapping_children() {
#if JELLYFRAME_SCRIPT_TASK_GROUP_API
    LayerNode root; root.type = LayerType::Root; root.bounds = {0, 0, 24, 16};
    LayerNodePtr group(new LayerNode(), LayerNodeDeleter{});
    group->type = LayerType::Composited; group->bounds = root.bounds; group->opacity = 0.5F;
    DisplayCommand red; red.rect = {2, 2, 12, 10}; red.color = {240, 0, 0, 255};
    group->display_list.push_back(red); red.rect.x = 8; group->display_list.push_back(red);
    root.children.push_back(std::move(group));
    const auto output = ScriptTaskFrameRenderer().render(composition_frame(root, root.bounds), {0, 0, 0, 255});
    assert(output.pixel(4, 4).r == output.pixel(10, 4).r);
    assert(output.pixel(10, 4).r >= 119 && output.pixel(10, 4).r <= 121);
#endif
}

void v6_fractional_image_painter_is_checked_before_group_publication() {
#if JELLYFRAME_SCRIPT_TASK_GROUP_API && defined(JELLYFRAME_RENDER_CORE_FRACTIONAL_IMAGE_SAMPLING_API_VERSION)
    LayerNode root; root.type=LayerType::Root; root.bounds={0,0,32,32};
    LayerNodePtr group(new LayerNode(),LayerNodeDeleter{});
    group->type=LayerType::Composited;group->bounds={5,4,21,20};
    group->has_clip=true;group->clip_rect={6,5,19,18};group->clip_border_radius=5;
    group->opacity=0.625F;group->transform.rotate_degrees=22.125F;
    DisplayCommand command;jellyframe::detail::set_command_type(command, DisplayCommandType::Image);command.rect={5,4,18,15};
    command.sampling_rect=command.rect;jellyframe::detail::command_image_handle(command)=7;
    assert(encode_fractional_sampling({{1,3},{1,4},{53,3},{59,4}},command.text));
    group->display_list.push_back(command);root.children.push_back(std::move(group));
    ImagePainter painter;
    painter.paint_fractional_clipped=[](FrameBuffer& target,const ImagePaintFractionalRequest& request,void*) {
        if(request.image_handle!=7)return false;
        for(int y=request.clip.y;y<safe_edge(request.clip.y,request.clip.height);++y)
            for(int x=request.clip.x;x<safe_edge(request.clip.x,request.clip.width);++x)
                target.pixel(x,y)={static_cast<std::uint8_t>(x*7),static_cast<std::uint8_t>(y*9),113,255};
        return true;
    };
    const Color background{13,17,29,255};
    FrameBuffer expected(32,32,background);
    assert(SoftwareCompositor({},painter).render_into_checked(root,expected,background).complete());
    auto frame=composition_frame(root,root.bounds);root.children.clear();
    ScriptTaskFrameRenderer renderer({},painter);
    const auto actual=renderer.render(frame,background);
    assert(equal_framebuffer_pixels(actual,expected));
    FrameBuffer target(32,32,{44,55,66,77});const auto before=target;
    auto missing=painter;missing.paint_fractional_clipped=nullptr;
    ScriptTaskFrameRenderStatus status;
    assert(!ScriptTaskFrameRenderer(missing).render_into(frame,target,background,nullptr,0,nullptr,&status));
    assert(status==ScriptTaskFrameRenderStatus::UnsupportedSamplingFeature);
    assert(equal_framebuffer_pixels(target,before));
#endif
}

} // namespace

void v6_damage_matches_full_frames_and_falls_back_at_contract_boundaries() {
#if JELLYFRAME_SCRIPT_TASK_GROUP_API
    ImagePainter image{bounded_marker_image_painter, nullptr, true};
    TextPainter bounded_text;
    bounded_text.writes_only_within_rect = true;
    bounded_text.paint = [](FrameBuffer& target, Rect rect, Color color, const std::string&,
                            int, int, TextCommandAlign, bool, void*) {
        for (int y=rect.y; y<rect.y+rect.height; ++y)
            for (int x=rect.x; x<rect.x+rect.width; ++x)
                if (target.contains(x,y)) target.pixel(x,y)=color;
        return true;
    };
    ScriptTaskFrameRenderer renderer(bounded_text, image);
    LayerNode root; root.type = LayerType::Root; root.bounds = {0, 0, 96, 80};
    DisplayCommand background; background.rect = root.bounds; background.color = {13, 17, 29, 255};
    root.display_list.push_back(background);
    LayerNodePtr card(new LayerNode(), LayerNodeDeleter{});
    card->bounds = {3, 2, 89, 76}; card->has_clip = true;
    card->clip_rect = card->bounds; card->clip_border_radius = 11; card->opacity = 0.75F;
    background.rect = card->bounds; background.color = {230, 240, 250, 210};
    card->display_list.push_back(background);
    DisplayCommand picture; jellyframe::detail::set_command_type(picture, DisplayCommandType::Image);
    picture.rect = {8, 7, 35, 21}; jellyframe::detail::command_image_handle(picture) = 1;
    card->display_list.push_back(picture);
    LayerNodePtr button(new LayerNode(), LayerNodeDeleter{});
    button->bounds = {17, 39, 63, 25}; button->has_clip = true;
    button->clip_rect = button->bounds; button->clip_border_radius = 8;
    background.rect = button->bounds; background.color = {20, 110, 220, 180};
    button->display_list.push_back(background);
    DisplayCommand label; jellyframe::detail::set_command_type(label, DisplayCommandType::Text); label.rect = {23, 43, 45, 15};
    label.text = "ROUND"; label.color = {250, 250, 250, 255};
    button->display_list.push_back(label);
    card->children.push_back(std::move(button));
    // Unchanged translucent overlay must also be replayed over new underlays.
    LayerNodePtr overlay(new LayerNode(), LayerNodeDeleter{});
    background.rect = {41, 48, 42, 22}; background.color = {220, 30, 70, 80};
    overlay->bounds = background.rect; overlay->display_list.push_back(background);
    card->children.push_back(std::move(overlay));
    root.children.push_back(std::move(card));
    auto previous = composition_frame(root, root.bounds);
    const Color clear{4, 8, 12, 255};
    FrameBuffer retained = renderer.render(previous, clear);
    assert(plan_script_task_group_damage(renderer, previous, previous).kind == ScriptTaskFrameDamageKind::Unchanged);
    unsigned partial = 0;
    for (int step = 0; step < 100; ++step) {
        auto& paint = *root.children[0]->children[0];
        paint.clip_border_radius = step % 2 ? 0 : 9;
        paint.opacity = step % 3 ? 1.0F : 0.625F;
        paint.display_list[0].color = {static_cast<std::uint8_t>(30 + step), 90, 210, 190};
        paint.display_list[0].rect.x = 15 + step % 9;
        paint.display_list[1].text = step % 2 ? "SQUARE" : "ROUND";
        paint.display_list[1].rect.x = 22 + step % 4;
        auto current = composition_frame(root, root.bounds);
        // Ellipse addition, removal and value changes are independent of the
        // command record itself; cover all three transitions.
        if (step % 3) current.command_ellipses.push_back({3, {{7, 4, 8, 2, 3, 7, 2, 8}}});
        const auto damage = plan_script_task_group_damage(renderer, previous, current);
        assert(damage.kind == ScriptTaskFrameDamageKind::Partial);
        assert(damage.rect.width * damage.rect.height < 96 * 80);
        ++partial;
        assert(renderer.render_into(current, retained, clear, &damage.rect, 1));
        const auto canonical = renderer.render(current, clear);
        assert(canonical.pixels.size() == retained.pixels.size());
        for (std::size_t i = 0; i < retained.pixels.size(); ++i) {
            const auto a = retained.pixels[i], b = canonical.pixels[i];
            assert(a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a);
        }
        previous = std::move(current);
    }
    assert(partial == 100);
    auto changed = previous;
    changed.input_targets.push_back({});
    assert(plan_script_task_group_damage(renderer, previous, changed).kind == ScriptTaskFrameDamageKind::Unchanged);
    changed = previous; changed.groups[1].transform.translate_x = 1;
    assert(plan_script_task_group_damage(renderer, previous, changed).kind == ScriptTaskFrameDamageKind::Full);
    changed = previous; changed.display_list.pop_back();
    assert(plan_script_task_group_damage(renderer, previous, changed).kind == ScriptTaskFrameDamageKind::Full);
    changed = previous; changed.groups[2].parent = 9999;
    assert(plan_script_task_group_damage(renderer, previous, changed).kind == ScriptTaskFrameDamageKind::Full);
    changed = previous; changed.clip_metadata_overflow = true;
    assert(plan_script_task_group_damage(renderer, previous, changed).kind == ScriptTaskFrameDamageKind::Full);
    changed = previous; changed.viewport.width++;
    assert(plan_script_task_group_damage(renderer, previous, changed).kind == ScriptTaskFrameDamageKind::Full);
    assert(plan_script_task_group_damage(ScriptTaskFrameRenderer(image), previous, previous).kind ==
        ScriptTaskFrameDamageKind::Full);
#endif
}

int script_task_frame_sampling_tests_main() {
    frame_diff_reports_value_churn_without_granting_reuse();
    value_frame_renderer_rejects_unsupported_sampling_without_writing();
    std::cout << "script task sampling transport/render-refusal tests passed\n";
    return 0;
}

template<class Command> void unknown_typed_command_is_refused_before_frame_writes() {
    if constexpr (detail::has_typed_command<Command>::value) {
        using Type = decltype(detail::command_type(std::declval<const Command&>()));
        ScriptTaskAppFrame frame;
        frame.viewport = {0, 0, 8, 8};
        Command fill;
        fill.rect = frame.viewport;
        fill.color = {255, 0, 0, 255};
        Command shadow;
        if constexpr (detail::has_css_shadow_type<Type>::value) {
            detail::set_command_type(shadow, Type::CssBoxShadow);
            if constexpr (detail::has_css_shadow_paint<Command>::value)
                shadow.css_shadow_paint().border_box = frame.viewport;
        } else detail::set_command_type(shadow, Type::PreparedText);
        shadow.rect = frame.viewport;
        frame.display_list = {fill, shadow};
        FrameBuffer target(8, 8, {11, 22, 33, 255});
        const auto before = target;
        ScriptTaskFrameRenderer renderer;
        ScriptTaskFrameRenderStatus status;
        assert(!renderer.render_into(frame, target, {}, nullptr, 0, nullptr, &status));
        assert(status == ScriptTaskFrameRenderStatus::InvalidFrame);
        assert(equal_framebuffer_pixels(target, before));
        Rect bounds;
        assert(!script_task_frame_command_visual_bounds(shadow, bounds));
    }
}

void shaped_sidecars_are_never_silently_dropped() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0,0,8,8};
    ScriptTaskFrameGroup root;
    root.type = LayerType::Root; root.bounds = frame.viewport;
    frame.groups.push_back(root);
    ScriptTaskFrameShapedText item;
    item.bounds = item.clip = frame.viewport; item.packet = "JFSP";
    frame.shaped_text.push_back(item);
    ScriptTaskAppFrameCodecOptions limits;
    limits.version = 1; limits.max_commands = 4; limits.max_text_bytes = 4096;
    limits.max_payload_bytes = 16384; limits.max_groups = 1;
    std::vector<std::uint8_t> bytes{0x5a};
    assert(encode_script_task_app_frame(frame, limits, bytes) == ScriptTaskAppFrameCodecStatus::UnsupportedTextFeature);
    assert(bytes == std::vector<std::uint8_t>{0x5a});
    if (!JELLYFRAME_RUNTIME_SHAPED_TEXT_API) {
        limits.version = kRuntimeShapedFrameVersion;
        assert(!script_task_app_frame_codec_supports_version(kRuntimeShapedFrameVersion));
        assert(encode_script_task_app_frame(frame, limits, bytes) == ScriptTaskAppFrameCodecStatus::UnsupportedVersion);
        ScriptTaskAppFrame unchanged;
        unchanged.viewport = {1,2,3,4};
        assert(decode_script_task_app_frame(bytes, limits, unchanged) == ScriptTaskAppFrameCodecStatus::UnsupportedVersion);
        assert(unchanged.viewport.x == 1 && unchanged.viewport.height == 4);
        ScriptTaskFrameRenderer renderer;
        FrameBuffer target(8,8,{1,2,3,255}), before = target;
        assert(!renderer.render_into(frame, target, {}) && equal_framebuffer_pixels(target, before));
    }
}

int script_task_frame_renderer_tests_main() {
#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API && JELLYFRAME_RUNTIME_IMAGE_API
    installed_scalar_replay_pins_resources_and_preserves_tiles();
#endif
    shaped_sidecars_are_never_silently_dropped();
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
    abi4_v11_shaped_text_codec_renderer_and_retained_replay();
#if JELLYFRAME_RUNTIME_SHAPED_PARAGRAPH_API
    abi4_v11_multiline_capture_damage_and_refusal();
#endif
#endif
    unknown_typed_command_is_refused_before_frame_writes<DisplayCommand>();
    v6_damage_matches_full_frames_and_falls_back_at_contract_boundaries();
    v6_groups_match_compositor_across_nested_effects_and_dirty_tiles();
    v6_group_frame_fixes_legacy_source_surface_fringe();
    v6_opacity_is_applied_after_overlapping_children();
    v6_fractional_image_painter_is_checked_before_group_publication();
    renderer_applies_nested_value_clips();
    renderer_consumes_codec_round_tripped_v2_frame();
    renderer_applies_value_frame_rotation_with_a_rounded_clip();
    renderer_matches_layer_compositor_for_right_angle_value_rotation();
    renderer_applies_ancestor_clip_only_in_destination_space_for_transforms();
    renderer_accepts_degenerate_value_frame_transform_as_empty_paint();
    renderer_matches_transformed_layer_own_clip();
    renderer_matches_nested_source_clips_for_transformed_child_content();
    renderer_matches_layer_compositor_for_translucent_clip_run();
    renderer_keeps_non_dirty_pixels_and_rejects_bad_chain();
    renderer_bounds_large_dirty_input_before_normalization();
    renderer_exposes_rounded_dirty_fast_path_statistics();
    renderer_forwards_opt_in_rounded_replay_timing();
    frame_diff_reports_value_churn_without_granting_reuse();
    frame_diff_accumulator_owns_only_accepted_value_frames();
    retained_replay_rebuilds_stable_underlap_and_verifies_pixels();
    retained_replay_uses_conservative_fallbacks_and_commit_boundary();
    retained_replay_rejects_unbounded_host_painters();
    abi4_renderer_refuses_legacy_text_without_writing();
    retained_replay_accepts_explicitly_bounded_image_painter();
    retained_replay_matches_canonical_output_for_background_clip_mutation();
    value_frame_renderer_rejects_unsupported_sampling_without_writing();
    retained_replay_matches_canonical_output_across_local_mutations();
    retained_replay_matches_canonical_output_across_nested_clip_mutations();
    retained_replay_falls_back_for_contract_boundary_changes();
    retained_replay_matches_canonical_output_across_pseudorandom_mutations();
    retained_replay_preserves_last_presented_state_across_rejections();
    std::cout << "script task frame renderer tests passed\n";
    return 0;
}
