#pragma once
#include "app_runtime/script_task_canonical_codec.h"
#include "app_runtime/script_task_canonical_renderer.h"
#include "render_core/raster_primitives.h"
#include "runtime_font_test_fixture.h"
#include <cassert>
#include <iostream>

namespace jellyframe::canonical_codec_tests {
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API
inline bool same_pixels(const FrameBuffer& a, const FrameBuffer& b) {
    if (a.width != b.width || a.height != b.height || a.pixels.size() != b.pixels.size()) return false;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        const auto x = a.pixels[i], y = b.pixels[i];
        if (x.r != y.r || x.g != y.g || x.b != y.b || x.a != y.a) return false;
    }
    return true;
}
inline bool image(FrameBuffer& target, const ImagePaintRequest& request, void*) {
    if (request.image_handle != 73) return false;
    for (int y = 0; y < target.height; ++y) for (int x = 0; x < target.width; ++x) {
        const auto r = request.destination;
        if (x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height) {
            const auto red = static_cast<std::uint8_t>((x - r.x) * 255 / r.width);
            const auto green = static_cast<std::uint8_t>((y - r.y) * 255 / r.height);
            target.pixel(x, y) = {red, green, 113, 255};
        }
    }
    return true;
}
class OwnedImages final : public RuntimeImageProviderSource {
public:
    std::uint64_t epoch = 1;
    std::uint64_t generation() const noexcept override { return epoch; }
    bool contains(std::uint32_t h) const noexcept override { return h == 73; }
    bool resolve(std::string_view, ResolvedImageResource& out) const override { out.image_handle = 73; return true; }
    bool paint(FrameBuffer& target, const ImagePaintRequest& request) const override { return image(target, request, nullptr); }
};
inline RuntimeImageProvider::Handle owned_images() {
    RuntimeImageProvider::Handle out;
    assert(RuntimeImageProvider::create(std::make_shared<OwnedImages>(), out) == RuntimeImageProviderStatus::Ready);
    return out;
}
#if JELLYFRAME_RENDER_CORE_PREPARED_TEXT_PACKET_VERSION >= 2
inline void signed_packet_v2() {
    using Status = ScriptTaskAppFrameCodecStatus;
    for (const auto version : {std::uint8_t{9}, std::uint8_t{10}}) {
        if (!script_task_app_frame_codec_supports_version(version)) continue;
        for (const std::int32_t initial_x : {-6250, 7250}) {
            runtime_test_fonts::Fonts fonts;
            TextFontRequest request; request.size_q1000 = 16000;
            TextRunMetrics metrics; PreparedTextGlyph glyphs[2];
            assert(fonts.catalog->text_system().prepare("AB", request, glyphs, 2, metrics) == TextSystemStatus::Ok);
            for (auto& glyph : glyphs) glyph.x += initial_x;
            DisplayCommand text; text.set_type(DisplayCommandType::PreparedText);
            text.rect = {0, 0, 64, 64}; text.color = {17, 73, 191, 255};
            text.text_paint().origin_x = 20; text.text_paint().baseline_y = 32;
            assert(encode_prepared_text(glyphs, 2, request.size_q1000, 2, text.text, initial_x));
            assert(text.text[4] == 2);
            PreparedTextView view; PreparedTextGlyph first;
            assert(decode_prepared_text(text.text, view) && view.read(0, first) && first.x == initial_x);
            LayerNode root; root.type = LayerType::Root; root.bounds = text.rect;
            root.display_list.push_back(text);
            SoftwareCompositor::Options core_options; core_options.text_system = &fonts.catalog->text_system();
            SoftwareCompositor core({}, {}, core_options);
            const Color background{255, 255, 255, 255};
            const auto expected = core.render(root, 64, 64, background);
            assert(std::any_of(expected.pixels.begin(), expected.pixels.end(),
                [](Color color) { return color.r != 255 || color.g != 255 || color.b != 255; }));
            ScriptTaskAppFrameCodecOptions options; options.version = version;
            options.max_commands = 8; options.max_groups = 8; options.max_text_bytes = 1024;
            options.max_payload_bytes = 8192;
            ScriptTaskAppFrame frame, decoded;
            assert(make_script_task_group_frame(root, root.bounds, options, frame) == Status::Accepted);
            std::vector<std::uint8_t> bytes, repeated;
            assert(encode_script_task_app_frame(frame, options, bytes) == Status::Accepted);
            assert(decode_script_task_app_frame(bytes, options, decoded) == Status::Accepted);
            assert(decoded.display_list[0].text == text.text);
            assert(decode_prepared_text(decoded.display_list[0].text, view) && view.read(0, first) && first.x == initial_x);
            assert(encode_script_task_app_frame(decoded, options, repeated) == Status::Accepted && repeated == bytes);
            auto malformed = bytes;
            const auto packet_at = detail::canonical::header_bytes + detail::canonical::group_bytes(version) +
                detail::canonical::command_bytes;
            malformed[packet_at + 4] = 255;
            ScriptTaskAppFrame sentinel; sentinel.viewport = {91, 92, 93, 94};
            assert(decode_script_task_app_frame(malformed, options, sentinel) == Status::Malformed);
            assert(sentinel.viewport.x == 91 && sentinel.display_list.empty());
            auto unsupported = frame; unsupported.display_list[0].text[4] = static_cast<char>(255);
            repeated = {3, 4, 5};
            assert(encode_script_task_app_frame(unsupported, options, repeated) == Status::InvalidValue);
            assert((repeated == std::vector<std::uint8_t>{3, 4, 5}));
            ScriptTaskSupervisor supervisor({{2,24},{1,0},{1,8192,8192},0,0});
            const auto session = supervisor.begin(181, fonts.catalog);
            ScriptTaskAppFramePublisher publisher(options);
            assert(publisher.publish(supervisor, session, decoded).accepted());
            ScriptTaskAcceptedAppFrame accepted;
            assert(take_script_task_app_frame(supervisor, session, options, accepted) == ScriptTaskAppFrameTakeStatus::Accepted);
            supervisor.begin(182, {}); fonts.catalog.reset(); root.display_list.clear();
            ScriptTaskFrameRenderer renderer;
            FrameBuffer replay(64, 64, background);
            assert(renderer.render_into(accepted, replay, background) && same_pixels(expected, replay));
            assert(accepted.frame().display_list[0].text == text.text);
            assert(renderer.render_into(accepted, replay, background) && same_pixels(expected, replay));
            assert(accepted.frame().display_list[0].text == text.text);
        }
    }
    std::cout << "canonical v9/v10: genuine v2 signed Q1000 origin roundtrip, atomic refusal and retained catalog pixels passed\n";
}
#endif

inline void rendering() {
    using Status = ScriptTaskAppFrameCodecStatus;
    runtime_test_fonts::Fonts fonts;
    auto catalog = fonts.catalog;
    auto images = owned_images();
    LayerNode root; root.type = LayerType::Root; root.bounds = {0, 0, 64, 64};
    LayerNodePtr child(new LayerNode, LayerNodeDeleter{});
    child->type = LayerType::Composited; child->bounds = root.bounds; child->has_clip = true;
    child->clip_rect = {5, 5, 54, 54}; child->clip_border_radius = 4; child->opacity = 0.625F;
    child->transform = {2, -1, 0.9F, 1.1F, 5}; child->has_transform = true;
    for (unsigned i = 0; i < 3; ++i) {
        DisplayCommand c; assert(detail::canonical::set_kind(c, 2 + i)); c.rect = {0, 0, 64, 64};
        c.color = {241, 31, 17, 219}; c.color2 = {11, 73, 239, 157};
        c.basic_paint().gradient_axis = GradientAxis::Horizontal; c.basic_paint().gradient_stop_percent = 67;
        c.basic_paint().border_radius = 3;
        detail::set_command_background_paint_clip(c, {i == 1 ? 30 : 8, i == 2 ? 30 : 8, 20, 20});
        child->display_list.push_back(c);
    }
    DisplayCommand img; img.set_type(DisplayCommandType::Image); img.rect = {28, 28, 32, 28};
    img.image_paint().image_handle = 73; img.image_paint().border_radius = 3;
    detail::set_command_background_paint_clip(img, {4, 3, 22, 19}); child->display_list.push_back(img);
    TextFontRequest request; request.size_q1000 = 16125; TextRunMetrics metrics; PreparedTextGlyph glyphs[2];
    assert(catalog->text_system().prepare("AB", request, glyphs, 2, metrics) == TextSystemStatus::Ok);
    DisplayCommand text; text.set_type(DisplayCommandType::PreparedText); text.rect = {0, 0, 64, 64};
    text.color = {33, 249, 19, 255}; text.text_paint().origin_x = 8; text.text_paint().baseline_y = 53;
    assert(encode_prepared_text(glyphs, 2, request.size_q1000, 2, text.text)); child->display_list.push_back(text);
    root.children.push_back(std::move(child));
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    {
        ScriptTaskAppFrameCodecOptions cropped_options; cropped_options.version = 9; cropped_options.image_provider_id = images->id();
        cropped_options.max_commands = 16; cropped_options.max_text_bytes = 1024;
        cropped_options.max_payload_bytes = 8192; cropped_options.max_groups = 8;
        ScriptTaskAppFrame unsupported;
        assert(make_script_task_group_frame(root, root.bounds, cropped_options, unsupported) == Status::Accepted);
        ScriptTaskFrameRendererOptions cropped_render; cropped_render.text_catalog = catalog; cropped_render.image_provider = images;
        ScriptTaskFrameRenderer cropped({}, ImagePainter{image, nullptr, true}, cropped_render);
        FrameBuffer target(64,64,{1,2,3,4}); const auto previous = target;
        assert(!cropped.render_into(unsupported, target, {}) && same_pixels(target, previous));
        root.children[0]->display_list.erase(root.children[0]->display_list.begin(), root.children[0]->display_list.begin() + 3);
    }
#endif
    ImagePainter painter{image, nullptr, true};
    VectorDiagnosticSink core_diagnostics;
    SoftwareCompositor::Options core_options; core_options.text_system = &catalog->text_system();
    core_options.diagnostics = &core_diagnostics;
    SoftwareCompositor core({}, painter, core_options);
    const Color background{249, 247, 241, 255}; const auto expected = core.render(root, 64, 64, background);
    assert(core_diagnostics.empty());
    ScriptTaskAppFrameCodecOptions options; options.version = 9; options.image_provider_id = images->id(); options.max_commands = 16;
    options.max_text_bytes = 1024; options.max_payload_bytes = 8192; options.max_groups = 8;
    ScriptTaskAppFrame captured, decoded;
    assert(make_script_task_group_frame(root, root.bounds, options, captured) == Status::Accepted);
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(captured, options, bytes) == Status::Accepted);
    assert(decode_script_task_app_frame(bytes, options, decoded) == Status::Accepted);
    ScriptTaskFrameRendererOptions render_options; render_options.text_catalog = catalog; render_options.image_provider = images;
    ScriptTaskFrameRenderer renderer({}, painter, render_options);
    ScriptTaskFrameRenderStatus status;
    const auto actual = renderer.render(decoded, background, &status);
    assert(status == ScriptTaskFrameRenderStatus::Accepted && same_pixels(expected, actual));
    assert(plan_script_task_group_damage(renderer, decoded, decoded).kind == ScriptTaskFrameDamageKind::Unchanged);
    auto changed = decoded;
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    changed.display_list[0].basic_paint().background_paint_clip.dx++;
#else
    changed.display_list[0].image_paint().background_paint_clip.dx++;
#endif
    assert(plan_script_task_group_damage(renderer, decoded, changed).kind == ScriptTaskFrameDamageKind::Full);
    changed = decoded; changed.display_list.back().text_paint().origin_x++;
    assert(plan_script_task_group_damage(renderer, decoded, changed).kind == ScriptTaskFrameDamageKind::Full);
    changed = decoded; changed.groups[1].opacity = 0.5F;
    assert(plan_script_task_group_damage(renderer, decoded, changed).kind == ScriptTaskFrameDamageKind::Full);
    changed = decoded; changed.groups[1].transform.translate_x++;
    assert(plan_script_task_group_damage(renderer, decoded, changed).kind == ScriptTaskFrameDamageKind::Full);
    FrameBuffer sentinel(64, 64, {1, 2, 3, 4}); const auto unchanged = sentinel;
    ScriptTaskFrameRenderer no_catalog({}, painter);
    assert(!no_catalog.render_into(decoded, sentinel, background, nullptr, 0, nullptr, &status));
    assert(same_pixels(sentinel, unchanged));
    runtime_test_fonts::Fonts unrelated;
    render_options.text_catalog = unrelated.catalog; ScriptTaskFrameRenderer wrong({}, painter, render_options);
    assert(!wrong.render_into(decoded, sentinel, background) && same_pixels(sentinel, unchanged));
    changed = decoded; changed.display_list.back().text[32 + 8] = 0x7f; // Valid packet, nonexistent glyph.
    assert(!renderer.render_into(changed, sentinel, background) && same_pixels(sentinel, unchanged));
    changed = decoded; changed.display_list[decoded.display_list.size() - 2].image_paint().image_handle = 999;
    assert(!renderer.render_into(changed, sentinel, background) && same_pixels(sentinel, unchanged));
    render_options.text_catalog = catalog; render_options.image_provider = images; render_options.max_temporary_pixels = 1;
    ScriptTaskFrameRenderer tiny({}, painter, render_options);
    assert(!tiny.render_into(decoded, sentinel, background) && same_pixels(sentinel, unchanged));
    // A sealed accepted envelope owns the exact session catalog after producer
    // handles and source tree disappear. A different session cannot take it.
    ScriptTaskSupervisor supervisor({{2,24},{1,0},{1,8192,8192},0,0});
    const auto session = supervisor.begin(81, catalog, images); assert(session.valid());
    ScriptTaskAppFramePublisher publisher(options); assert(publisher.publish(supervisor, session, captured).accepted());
    ScriptTaskAcceptedAppFrame accepted;
    assert(take_script_task_app_frame(supervisor, session, options, accepted) == ScriptTaskAppFrameTakeStatus::Accepted);
    fonts.catalog.reset(); catalog.reset(); root.children.clear();
    render_options.text_catalog.reset();
    FrameBuffer replay(64,64,background); ScriptTaskFrameRenderer bound({}, painter);
    assert(bound.render_into(accepted, replay, background) && same_pixels(replay, expected));
    ScriptTaskSupervisor no_images({{2,24},{1,0},{1,8192,8192},0,0});
    const auto unbound_session = no_images.begin(82, accepted.text_catalog());
    assert(publisher.publish(no_images, unbound_session, captured).accepted());
    assert(take_script_task_app_frame(no_images, unbound_session, options, accepted) == ScriptTaskAppFrameTakeStatus::ImageProviderRejected);
    auto wrong_images = owned_images(); auto bad = captured; bad.image_provider_id = wrong_images->id();
    assert(publisher.publish(supervisor, session, bad).accepted());
    const auto sequence = accepted.sequence();
    assert(take_script_task_app_frame(supervisor, session, options, accepted) == ScriptTaskAppFrameTakeStatus::ImageProviderRejected);
    assert(accepted.sequence() == sequence);
    auto wrong_options = render_options; wrong_options.text_catalog = accepted.text_catalog(); wrong_options.image_provider = wrong_images;
    ScriptTaskFrameRenderer wrong_owner({}, painter, wrong_options);
    assert(!wrong_owner.render_into(captured, sentinel, background) && same_pixels(sentinel, unchanged));
    auto reset_source = std::make_shared<OwnedImages>(); RuntimeImageProvider::Handle reset_owner;
    assert(RuntimeImageProvider::create(reset_source, reset_owner) == RuntimeImageProviderStatus::Ready);
    bad = captured; bad.image_provider_id = reset_owner->id();
    wrong_options.image_provider = reset_owner; ScriptTaskFrameRenderer reset_renderer({}, painter, wrong_options);
    ++reset_source->epoch; // Deliberate sequential contract violation, detected before paint.
    assert(!reset_renderer.render_into(bad, sentinel, background) && same_pixels(sentinel, unchanged));
    supervisor.begin_teardown(session); supervisor.complete_teardown(session);
    images.reset(); render_options.image_provider.reset();
    assert(bound.render_into(accepted, replay, background) && same_pixels(replay, expected));
    const ScriptAppSession stale{session.app_instance_id, session.generation + 1};
    assert(take_script_task_app_frame(supervisor, stale, options, accepted) == ScriptTaskAppFrameTakeStatus::TextCatalogRejected);
    assert(bound.render_into(accepted, replay, background) && same_pixels(replay, expected));
    std::cout << "canonical v9 pixels: direct Core == decoded renderer for image/"
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
                 "all gradients/"
#else
                 "cropped modern-paint refusal/"
#endif
                 "JFTP1/nested group; "
                 "catalog ownership, stale/missing resources, atomic paint refusal and conservative retained damage passed\n";
}
#endif
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API
template<class Command, class Node> inline void elliptical_v10_round_trip() {
    if constexpr (detail::canonical::has_radius_y_fields<Command>::value &&
                  detail::canonical::has_layer_radius_y<Node>::value) {
        using Status = ScriptTaskAppFrameCodecStatus;
        Node root; root.type = LayerType::Root; root.bounds = {0, 0, 48, 40};
        LayerNodePtr child(new Node, LayerNodeDeleter{});
        child->type = LayerType::Composited; child->bounds = root.bounds; child->has_clip = true;
        child->clip_rect = {3, 2, 42, 36}; child->clip_border_radius = 19;
        detail::canonical::set_layer_radius_y(*child, 7);
        Command fill; assert(detail::canonical::set_kind(fill, 0)); fill.rect = {0, 0, 48, 40};
        fill.color = {17, 113, 221, 255}; fill.basic_paint().border_radius = 16;
        fill.basic_paint().border_radius_y = 5; child->display_list.push_back(fill);
        root.children.push_back(std::move(child));

        ScriptTaskAppFrameCodecOptions options; options.version = 10; options.max_commands = 4;
        options.max_text_bytes = 128; options.max_payload_bytes = 4096; options.max_groups = 4;
        options.max_clips = 4; options.max_input_targets = 4;
        ScriptTaskAppFrame captured, decoded;
        assert(make_script_task_group_frame(root, root.bounds, options, captured) == Status::Accepted);
        captured.clips.push_back({{1, 1, 44, 38}, 13, kScriptTaskNoParentClip, 4});
        std::vector<std::uint8_t> bytes;
        assert(encode_script_task_app_frame(captured, options, bytes) == Status::Accepted && bytes[4] == 10);
        assert(decode_script_task_app_frame(bytes, options, decoded) == Status::Accepted);
        assert(decoded.display_list[0].basic_paint().border_radius == 16 &&
               detail::canonical::radius_y(decoded.display_list[0]) == 5);
        assert(decoded.groups[1].clip_border_radius == 19 && decoded.groups[1].clip_border_radius_y == 7);
        assert(decoded.clips[0].border_radius == 13 && decoded.clips[0].border_radius_y == 4);
        std::vector<std::uint8_t> reencoded;
        assert(encode_script_task_app_frame(decoded, options, reencoded) == Status::Accepted && reencoded == bytes);
        assert(bytes.size() == 52 + 28 + 2 * 116 + 128);

        const auto expected = SoftwareCompositor().render(root, 48, 40, {249, 247, 241, 255});
        ScriptTaskFrameRenderer renderer;
        ScriptTaskFrameRenderStatus render_status;
        const auto actual = renderer.render(decoded, {249, 247, 241, 255}, &render_status);
        assert(render_status == ScriptTaskFrameRenderStatus::Accepted && same_pixels(expected, actual));

        auto payloads = captured;
        Command text; assert(detail::canonical::set_kind(text, 6)); text.rect = {0, 0, 1, 1}; text.text = "T";
        text.text_paint().border_radius = 9; text.text_paint().border_radius_y = 3;
        payloads.display_list.push_back(text);
        Command image; assert(detail::canonical::set_kind(image, 8)); image.rect = {0, 0, 1, 1};
        image.image_paint().image_handle = 73; image.image_paint().border_radius = 11;
        image.image_paint().border_radius_y = 2; payloads.display_list.push_back(image);
        payloads.image_provider_id = 77; payloads.groups[1].command_count = 3;
        assert(encode_script_task_app_frame(payloads, options, bytes) == Status::Accepted);
        assert(decode_script_task_app_frame(bytes, options, decoded) == Status::Accepted);
        assert(decoded.display_list[1].text_paint().border_radius == 9 && detail::canonical::radius_y(decoded.display_list[1]) == 3);
        assert(decoded.display_list[2].image_paint().border_radius == 11 && detail::canonical::radius_y(decoded.display_list[2]) == 2);

        auto legacy = captured;
        auto legacy_options = options; legacy_options.version = 9;
        detail::canonical::set_radius_y(legacy.display_list[0], -1);
        legacy.groups[1].clip_border_radius_y = -1;
        legacy.clips[0].border_radius_y = -1;
        std::vector<std::uint8_t> v9_bytes;
        assert(encode_script_task_app_frame(legacy, legacy_options, v9_bytes) == Status::Accepted);
        auto equal_v9 = legacy;
        detail::canonical::set_radius_y(equal_v9.display_list[0], 16);
        equal_v9.groups[1].clip_border_radius_y = 19;
        equal_v9.clips[0].border_radius_y = 13;
        std::vector<std::uint8_t> equal_v9_bytes;
        assert(encode_script_task_app_frame(equal_v9, legacy_options, equal_v9_bytes) == Status::Accepted);
        assert(equal_v9_bytes == v9_bytes);
        ScriptTaskAppFrame decoded_v9;
        assert(decode_script_task_app_frame(v9_bytes, legacy_options, decoded_v9) == Status::Accepted);
        assert(detail::canonical::radius_y(decoded_v9.display_list[0]) == -1 &&
               decoded_v9.groups[1].clip_border_radius_y == -1 && decoded_v9.clips[0].border_radius_y == -1);
        auto unequal_v9 = legacy;
        detail::canonical::set_radius_y(unequal_v9.display_list[0], 5);
        reencoded = {3, 4, 5};
        assert(encode_script_task_app_frame(unequal_v9, legacy_options, reencoded) == Status::InvalidValue);
        assert((reencoded == std::vector<std::uint8_t>{3, 4, 5}));
        unequal_v9 = legacy; unequal_v9.groups[1].clip_border_radius_y = 7;
        assert(encode_script_task_app_frame(unequal_v9, legacy_options, reencoded) == Status::InvalidGroup);
        assert((reencoded == std::vector<std::uint8_t>{3, 4, 5}));
        unequal_v9 = legacy; unequal_v9.clips[0].border_radius_y = 4;
        assert(encode_script_task_app_frame(unequal_v9, legacy_options, reencoded) == Status::InvalidClip);
        assert((reencoded == std::vector<std::uint8_t>{3, 4, 5}));
        auto malformed = bytes;
        const std::size_t command_at = 52 + 28 + 2 * 116;
        detail::canonical::u32(malformed, command_at + 64 + 24, 0xffffffffU);
        ScriptTaskAppFrame untouched; untouched.viewport = {91, 92, 93, 94};
        assert(decode_script_task_app_frame(malformed, options, untouched) == Status::Malformed);
        assert(untouched.viewport.x == 91 && untouched.display_list.empty());
        malformed = bytes; detail::canonical::u32(malformed, 52 + 28 + 116 + 112, 0xffffffffU);
        assert(decode_script_task_app_frame(malformed, options, untouched) == Status::Malformed);
        malformed = bytes; malformed[4] = 9;
        assert(decode_script_task_app_frame(malformed, options, untouched) == Status::UnsupportedVersion);
        malformed = bytes; detail::canonical::u32(malformed, 36, 3);
        assert(decode_script_task_app_frame(malformed, options, untouched) == Status::Malformed);
        malformed = bytes; malformed.push_back(0);
        assert(decode_script_task_app_frame(malformed, options, untouched) == Status::Malformed);

        auto legacy_v9_options = options; legacy_v9_options.version = 9; reencoded = {3, 4, 5};
        assert(encode_script_task_app_frame(captured, legacy_v9_options, reencoded) == Status::InvalidGroup);
        assert((reencoded == std::vector<std::uint8_t>{3, 4, 5}));

        // Both axes must include signed, nonuniform per-corner encodings.
        const int packed_x = encode_corner_radii({19, 11, 15, 7});
        const int packed_y = encode_corner_radii({4, 8, 6, 2});
        assert(packed_x < -1 && packed_y < -1 && packed_x != packed_y);
        auto packed = payloads;
        for (auto& c : packed.display_list) {
            auto fields = detail::legacy_command_fields(c); fields.border_radius = packed_x;
            detail::apply_legacy_command_fields(c, fields);
            detail::canonical::set_radius_y(c, packed_y);
        }
        packed.groups[1].clip_border_radius = packed_x; packed.groups[1].clip_border_radius_y = packed_y;
        packed.clips[0].border_radius = packed_x; packed.clips[0].border_radius_y = packed_y;
        assert(encode_script_task_app_frame(packed, options, bytes) == Status::Accepted);
        assert(decode_script_task_app_frame(bytes, options, decoded) == Status::Accepted);
        for (const auto& c : decoded.display_list) {
            assert(detail::legacy_command_fields(c).border_radius == packed_x);
            assert(detail::canonical::radius_y(c) == packed_y);
        }
        assert(decoded.groups[1].clip_border_radius_y == packed_y && decoded.clips[0].border_radius_y == packed_y);
        assert(encode_script_task_app_frame(decoded, options, reencoded) == Status::Accepted && reencoded == bytes);

        auto packed_fill = captured;
        packed_fill.display_list[0] = packed.display_list[0];
        packed_fill.groups[1] = packed.groups[1]; packed_fill.groups[1].command_count = 1;
        packed_fill.clips = packed.clips;
        Node packed_root; packed_root.type = LayerType::Root; packed_root.bounds = root.bounds;
        LayerNodePtr packed_child(new Node, LayerNodeDeleter{});
        packed_child->type = LayerType::Composited; packed_child->bounds = root.bounds;
        packed_child->has_clip = true; packed_child->clip_rect = packed_fill.groups[1].clip_rect;
        packed_child->clip_border_radius = packed_x; detail::canonical::set_layer_radius_y(*packed_child, packed_y);
        packed_child->display_list = packed_fill.display_list; packed_root.children.push_back(std::move(packed_child));
        ScriptTaskAppFrame packed_capture;
        assert(make_script_task_group_frame(packed_root, root.bounds, options, packed_capture) == Status::Accepted);
        assert(packed_capture.groups[1].clip_border_radius_y == packed_y);
        const auto flattened = make_script_task_app_frame(packed_root, root.bounds, {}, true);
        bool flattened_y_preserved = false;
        for (const auto& clip : flattened.clips)
            flattened_y_preserved = flattened_y_preserved ||
                (clip.border_radius == packed_x && clip.border_radius_y == packed_y);
        assert(flattened_y_preserved);
        assert(encode_script_task_app_frame(packed_fill, options, bytes) == Status::Accepted);
        assert(decode_script_task_app_frame(bytes, options, decoded) == Status::Accepted);
        const auto packed_expected = SoftwareCompositor().render(packed_root, 48, 40, {249, 247, 241, 255});
        const auto packed_actual = renderer.render(decoded, {249, 247, 241, 255}, &render_status);
        assert(render_status == ScriptTaskFrameRenderStatus::Accepted && same_pixels(packed_expected, packed_actual));
        const auto flattened_actual = renderer.render(flattened, {249, 247, 241, 255}, &render_status);
        assert(render_status == ScriptTaskFrameRenderStatus::Accepted && same_pixels(packed_expected, flattened_actual));
        auto circular = packed_fill; detail::canonical::set_radius_y(circular.display_list[0], packed_x);
        circular.groups[1].clip_border_radius_y = packed_x;
        const auto circular_pixels = renderer.render(circular, {249, 247, 241, 255}, &render_status);
        assert(render_status == ScriptTaskFrameRenderStatus::Accepted && !same_pixels(packed_actual, circular_pixels));
        auto changed = packed_fill; detail::canonical::set_radius_y(changed.display_list[0], packed_x);
        assert(plan_script_task_group_damage(renderer, packed_fill, changed).kind == ScriptTaskFrameDamageKind::Full);
        changed = packed_fill; changed.groups[1].clip_border_radius_y = packed_x;
        assert(plan_script_task_group_damage(renderer, packed_fill, changed).kind == ScriptTaskFrameDamageKind::Full);

        packed_fill.input_targets.push_back({29, root.bounds, true, 0});
        bool hit_differs_from_circle = false;
        for (int y = 0; y < 40; ++y) for (int x = 0; x < 48; ++x) {
            const auto& clip = packed_fill.clips[0];
            // root.bounds keeps overload resolution dependent on this Core's Node API.
            auto clip_rect = root.bounds; clip_rect = clip.rect;
            const bool inside = rounded_rect_coverage(clip_rect, packed_x, packed_y, x, y) != 0;
            const bool circular_inside = rounded_rect_coverage(clip_rect, packed_x, x, y) != 0;
            hit_differs_from_circle = hit_differs_from_circle || inside != circular_inside;
            assert(resolve_script_task_input_target(packed_fill, x, y) == (inside ? 29U : 0U));
        }
        assert(hit_differs_from_circle);

        auto packed_legacy = packed;
        for (auto& c : packed_legacy.display_list) detail::canonical::set_radius_y(c, -1);
        packed_legacy.groups[1].clip_border_radius_y = -1; packed_legacy.clips[0].border_radius_y = -1;
        assert(encode_script_task_app_frame(packed_legacy, legacy_options, v9_bytes) == Status::Accepted);
        auto packed_equal = packed_legacy;
        for (auto& c : packed_equal.display_list) detail::canonical::set_radius_y(c, packed_x);
        packed_equal.groups[1].clip_border_radius_y = packed_x; packed_equal.clips[0].border_radius_y = packed_x;
        assert(encode_script_task_app_frame(packed_equal, legacy_options, equal_v9_bytes) == Status::Accepted && equal_v9_bytes == v9_bytes);
        assert(decode_script_task_app_frame(v9_bytes, legacy_options, decoded_v9) == Status::Accepted);
        for (const auto& c : decoded_v9.display_list) assert(detail::canonical::radius_y(c) == -1);
        assert(encode_script_task_app_frame(packed_legacy, options, bytes) == Status::Accepted);
        assert(decode_script_task_app_frame(bytes, options, decoded) == Status::Accepted);
        for (const auto& c : decoded.display_list) assert(detail::canonical::radius_y(c) == packed_x);
        assert(decoded.groups[1].clip_border_radius_y == packed_x && decoded.clips[0].border_radius_y == packed_x);
        const std::size_t y_offsets[] = {52 + 24, 52 + 28 + 116 + 112,
            command_at + 88, command_at + 128 + 100, command_at + 256 + 1 + 96};
        for (auto offset : y_offsets) for (auto invalid : {0xffffffffU, 0x90000000U, 0xf0000000U}) {
            malformed = bytes; detail::canonical::u32(malformed, offset, invalid);
            assert(decode_script_task_app_frame(malformed, options, untouched) == Status::Malformed);
            assert(untouched.viewport.x == 91 && untouched.display_list.empty());
        }
        auto packed_unequal = packed_legacy;
        detail::canonical::set_radius_y(packed_unequal.display_list[0], packed_y);
        reencoded = {3, 4, 5};
        assert(encode_script_task_app_frame(packed_unequal, legacy_options, reencoded) == Status::InvalidValue);
        packed_unequal = packed_legacy; packed_unequal.groups[1].clip_border_radius_y = packed_y;
        assert(encode_script_task_app_frame(packed_unequal, legacy_options, reencoded) == Status::InvalidGroup);
        packed_unequal = packed_legacy; packed_unequal.clips[0].border_radius_y = packed_y;
        assert(encode_script_task_app_frame(packed_unequal, legacy_options, reencoded) == Status::InvalidClip);
        assert((reencoded == std::vector<std::uint8_t>{3, 4, 5}));
        auto v4_options = options; v4_options.version = 4;
        auto flat_legacy = packed_legacy; flat_legacy.groups.clear(); flat_legacy.clips.clear();
        flat_legacy.image_provider_id = 0; flat_legacy.display_list.resize(1);
        auto flat_equal = flat_legacy;
        detail::canonical::set_radius_y(flat_equal.display_list[0], packed_x);
        assert(encode_script_task_app_frame(flat_legacy, v4_options, v9_bytes) == Status::Accepted);
        assert(encode_script_task_app_frame(flat_equal, v4_options, equal_v9_bytes) == Status::Accepted && equal_v9_bytes == v9_bytes);
        assert(decode_script_task_app_frame(v9_bytes, v4_options, decoded_v9) == Status::Accepted);
        assert(encode_script_task_app_frame(decoded_v9, v4_options, reencoded) == Status::Accepted && reencoded == v9_bytes);
        detail::canonical::set_radius_y(flat_equal.display_list[0], packed_y); reencoded = {3, 4, 5};
        assert(encode_script_task_app_frame(flat_equal, v4_options, reencoded) == Status::InvalidValue);
        assert((reencoded == std::vector<std::uint8_t>{3, 4, 5}));
        flat_legacy.clips.push_back({root.bounds, 13, kScriptTaskNoParentClip, -1});
        assert(encode_script_task_app_frame(flat_legacy, v4_options, v9_bytes) == Status::Accepted);
        flat_legacy.clips[0].border_radius_y = 13;
        assert(encode_script_task_app_frame(flat_legacy, v4_options, equal_v9_bytes) == Status::Accepted && equal_v9_bytes == v9_bytes);
        flat_legacy.clips[0].border_radius_y = 4;
        assert(encode_script_task_app_frame(flat_legacy, v4_options, reencoded) == Status::InvalidClip);
        assert((reencoded == std::vector<std::uint8_t>{3, 4, 5}));
        std::cout << "canonical v10: signed per-corner axes, sentinel normalization, v9 byte identity/refusal, "
                     "capture/replay pixels, Y-only damage and elliptical input clips passed\n";
    }
}
#endif
template<class Command> void configure_css_shadow(Command& command) {
    using Type = decltype(detail::command_type(command));
    if constexpr (detail::has_css_shadow_type<Type>::value && detail::has_css_shadow_paint<Command>::value) {
        detail::set_command_type(command, Type::CssBoxShadow);
        auto& paint = command.css_shadow_paint();
        paint.border_box = {1, 2, 30, 40}; paint.padding_box = {3, 4, 26, 36};
        paint.source_rect = {2, 3, 27, 37}; paint.border_radii = 2; paint.padding_radii = 1;
        paint.source_radii = 2; paint.blur_px = 0; paint.mode = decltype(paint.mode)::Inset; paint.flags = 0;
    }
}
inline void run() {
    using Status = ScriptTaskAppFrameCodecStatus;
    assert(script_task_app_frame_codec_supports_version(9) == detail::canonical::available);
    assert(script_task_app_frame_codec_supports_version(10) == detail::canonical::elliptical_available);
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API
    ScriptTaskAppFrameCodecOptions options;
    options.image_provider_id = (1ULL << 48) + 81;
    options.version = 9; options.max_commands = 16; options.max_text_bytes = 1024;
    options.max_payload_bytes = 8192; options.max_groups = 8; options.max_group_depth = 8;
    options.max_input_targets = 4; options.max_clips = 4;
    LayerNode root; root.type = LayerType::Root; root.bounds = {0, 0, 80, 80};
    root.reasons = LayerReasonRoot; root.source_order = 123;
    constexpr bool has_css_shadow = detail::has_css_shadow_type<DisplayCommandType>::value &&
        detail::has_css_shadow_paint<DisplayCommand>::value;
    for (unsigned kind = 0; kind < (has_css_shadow ? 10U : 9U); ++kind) {
        DisplayCommand c; if (!detail::canonical::set_kind(c, kind)) std::abort();
        c.rect = {1, 2, 30, 40}; c.color = {3, 7, 11, 197}; c.color2 = {31, 37, 41, 173};
        if (kind <= 5) {
            auto& p = c.basic_paint(); p.border_radius = 3; p.stroke_width = 2;
            p.gradient_axis = GradientAxis::DiagonalDownLeft; p.gradient_stop_percent = 67;
            if (kind >= 2 && kind <= 4) detail::set_command_background_paint_clip(c, {-32768, 32767, 65535, 1});
        } else if (kind <= 7) {
            auto& p = c.text_paint(); p.border_radius = 2; p.stroke_width = 3; p.font_size = 16;
            p.font_weight = 500; p.font_family_hash = 0x81234567U; p.text_align = TextCommandAlign::End;
            p.text_single_line = false; p.origin_x = -123; p.baseline_y = 456;
            if (kind == 6) c.text = "canonical raw text";
            else {
                PreparedTextGlyph g; g.resource_id = (1ULL << 48) + 17; g.glyph_id = 23;
                g.codepoint = g.source_codepoint = 65; g.source_begin = 0; g.source_end = 1; g.x = 0;
                g.geometry = {1250, -100, 1400, 900, 1500};
                if (!encode_prepared_text(&g, 1, 16125, 1, c.text)) std::abort();
            }
        } else if (kind == 8) {
            auto& p = c.image_paint(); p.image_handle = 0xfedcba98U; p.object_fit = ObjectFit::Cover;
            p.object_position = {-13, 157}; p.image_rendering = ImageRendering::CrispEdges; p.border_radius = 5;
            detail::set_command_background_paint_clip(c, {-32768, 32767, 65535, 1});
        } else {
            configure_css_shadow(c);
        }
        root.display_list.push_back(c);
    }
    LayerNodePtr child(new LayerNode, LayerNodeDeleter{});
    child->type = LayerType::Composited; child->bounds = {4, 5, 20, 25}; child->has_clip = true;
    child->clip_rect = {5, 6, 18, 21}; child->clip_border_radius = 3; child->opacity = 0.625F;
    child->transform = {1.25F, -2.5F, 0.75F, 1.125F, 12.5F}; child->has_transform = true;
    child->transform_origin_x_percent = -17; child->transform_origin_y_percent = 123;
    child->reasons = LayerReasonOpacity | LayerReasonTransform; child->scroll_y = 7; child->max_scroll_y = 29;
    child->z_index = -9; child->source_order = (1ULL << 40) + 73;
    root.children.push_back(std::move(child));
    ScriptTaskAppFrame frame;
    if (make_script_task_group_frame(root, root.bounds, options, frame) != Status::Accepted) std::abort();
    const auto command_count = has_css_shadow ? 10U : 9U;
    assert(frame.groups.size() == 2 && frame.display_list.size() == command_count);
    frame.clips.push_back({{0, 0, 60, 60}, 3, kScriptTaskNoParentClip});
    frame.input_targets.push_back({117, {1, 2, 10, 12}, true, 0});
    std::vector<std::uint8_t> bytes;
    if (encode_script_task_app_frame(frame, options, bytes) != Status::Accepted) std::abort();
    const std::size_t command_at = 52 + 28 + 2 * 112;
    if (bytes.size() != command_at + command_count * 128 + 18 + 84 + 24 ||
        bytes[4] != 9 || detail::canonical::u32(bytes, command_at) != 0) std::abort();
    // Stable kind7 and a genuine JFTP1 packet; resource identity keeps all64 bits.
    const auto prepared_at = command_at + 7 * 128 + 18;
    if (detail::canonical::u32(bytes, prepared_at) != 7 ||
        detail::canonical::i32(bytes, prepared_at + 92) != -123 ||
        detail::canonical::i32(bytes, prepared_at + 96) != 456 || bytes[prepared_at + 128 + 4] != 1) std::abort();
    ScriptTaskAppFrame decoded;
    const auto decode_status = decode_script_task_app_frame(bytes, options, decoded);
    if (decode_status != Status::Accepted) std::abort();
    for (std::size_t i = 0; i < frame.display_list.size(); ++i)
        assert(display_commands_equal(frame.display_list[i], decoded.display_list[i]));
    PreparedTextView view; PreparedTextGlyph glyph;
    assert(decode_prepared_text(decoded.display_list[7].text, view) && view.read(0, glyph));
    assert(glyph.resource_id == (1ULL << 48) + 17);
    if constexpr (!has_css_shadow) {
        auto unsupported = bytes;
        detail::canonical::u32(unsupported, command_at + (command_count - 1) * 128, 9);
        ScriptTaskAppFrame untouched; untouched.viewport = {91, 92, 93, 94};
        assert(decode_script_task_app_frame(unsupported, options, untouched) == Status::Malformed);
        assert(untouched.viewport.x == 91 && untouched.display_list.empty());
    }
    const auto& g = decoded.groups[1];
    assert(g.has_transform && g.has_clip && g.opacity == 0.625F && g.transform.translate_x == 1.25F &&
        g.transform.rotate_degrees == 12.5F && g.origin_x_percent == -17 && g.origin_y_percent == 123 &&
        g.scroll_y == 7 && g.max_scroll_y == 29 && g.z_index == -9 && g.source_order == (1ULL << 40) + 73 &&
        g.reasons == (LayerReasonOpacity | LayerReasonTransform));
    std::vector<std::uint8_t> reencoded;
    assert(encode_script_task_app_frame(decoded, options, reencoded) == Status::Accepted && reencoded == bytes);
    // Captured values and sealed bytes own all strings and contain no tree pointers.
    root.display_list[7].text.assign("producer replaced"); root.children.clear();
    assert(frame.display_list[7].text == decoded.display_list[7].text && frame.groups.size() == 2);

    const auto sentinel = [&] { decoded.viewport = {91, 92, 93, 94}; decoded.display_list.clear(); };
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        auto truncated = bytes; truncated.resize(length); sentinel();
        assert(decode_script_task_app_frame(truncated, options, decoded) != Status::Accepted);
        assert(decoded.viewport.x == 91 && decoded.viewport.width == 93 && decoded.display_list.empty());
    }
    // Reserved header/clip/payload, topology, nonfinite opacity, unknown kind,
    // invalid bool/clip reference, and JFTP packet version must refuse atomically.
    const std::size_t corrupt_offsets[] = {5, 40, 52 + 24, 52 + 28 + 52, 52 + 28 + 56,
        52 + 28 + 112, command_at, command_at + 32, command_at + 60, command_at + 64 + 24,
        prepared_at + 128 + 4};
    for (const auto offset : corrupt_offsets) {
        auto malformed = bytes;
        for (std::size_t j = 0; j < 4; ++j) malformed[offset + j] = 0xff;
        sentinel(); assert(decode_script_task_app_frame(malformed, options, decoded) != Status::Accepted);
        assert(decoded.viewport.x == 91 && decoded.display_list.empty());
    }
    auto trailing = bytes; trailing.push_back(0); sentinel();
    assert(decode_script_task_app_frame(trailing, options, decoded) == Status::Malformed && decoded.viewport.x == 91);
    auto exact = options; exact.max_payload_bytes = bytes.size(); exact.max_text_bytes = 18 + 84;
    assert(encode_script_task_app_frame(frame, exact, reencoded) == Status::Accepted && reencoded == bytes);
    --exact.max_payload_bytes; reencoded = {3, 4, 5};
    assert(encode_script_task_app_frame(frame, exact, reencoded) == Status::PayloadTooLarge);
    assert((reencoded == std::vector<std::uint8_t>{3, 4, 5}));
    exact = options; exact.max_text_bytes = 18 + 84 - 1;
    assert(encode_script_task_app_frame(frame, exact, reencoded) == Status::TooManyTextBytes);
    assert((reencoded == std::vector<std::uint8_t>{3, 4, 5}));
    auto missing_identity = frame; missing_identity.image_provider_id = 0;
    assert(encode_script_task_app_frame(missing_identity, options, reencoded) == Status::InvalidValue);
    auto legacy_options = options; legacy_options.version = 1;
    assert(encode_script_task_app_frame(frame, legacy_options, reencoded) == Status::InvalidValue);
    auto changed_namespace = frame; changed_namespace.image_provider_id++;
    ScriptTaskFrameRenderer namespace_renderer;
    assert(plan_script_task_group_damage(namespace_renderer, frame, changed_namespace).kind == ScriptTaskFrameDamageKind::Full);
    auto unsupported = frame; unsupported.groups[1].clip_axes = 1;
    assert(encode_script_task_app_frame(unsupported, options, reencoded) == Status::UnsupportedGroupFeature);
    unsupported = frame; unsupported.groups[1].has_affine = true;
    assert(encode_script_task_app_frame(unsupported, options, reencoded) == Status::UnsupportedGroupFeature);
    unsupported = frame; unsupported.command_ellipses.push_back({});
    assert(encode_script_task_app_frame(unsupported, options, reencoded) == Status::UnsupportedGroupFeature);
    unsupported = frame; unsupported.display_list[8].text = "JFSXunsupported-sampling";
    assert(encode_script_task_app_frame(unsupported, options, reencoded) == Status::InvalidValue);
    unsupported = frame; unsupported.display_list[7].text[4] = static_cast<char>(255);
    assert(encode_script_task_app_frame(unsupported, options, reencoded) == Status::InvalidValue);
    unsupported = frame; unsupported.groups[0].reasons = 1U << 31;
    assert(encode_script_task_app_frame(unsupported, options, reencoded) == Status::InvalidGroup);
    unsupported = frame; unsupported.groups[0].type = static_cast<LayerType>(255);
    assert(encode_script_task_app_frame(unsupported, options, reencoded) == Status::InvalidGroup);
    unsupported = frame; unsupported.display_list[0].transform.enabled = true;
    assert(encode_script_task_app_frame(unsupported, options, reencoded) == Status::UnsupportedTransformFeature);
    assert((reencoded == std::vector<std::uint8_t>{3, 4, 5}));
    std::cout << "canonical v9: 10 typed kinds/JFTP1/all paint clips, nested group values, "
              << bytes.size() << " atomic truncations and malformed/refusal checks passed\n";
#if JELLYFRAME_RENDER_CORE_PREPARED_TEXT_PACKET_VERSION >= 2
    signed_packet_v2();
#endif
    rendering();
    elliptical_v10_round_trip<DisplayCommand, LayerNode>();
#else
    std::cout << "canonical v9 unavailable on selected Core; legacy contracts retained\n";
#endif
}
} // namespace jellyframe::canonical_codec_tests
