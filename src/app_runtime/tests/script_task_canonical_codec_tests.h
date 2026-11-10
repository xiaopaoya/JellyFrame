#pragma once
#include "app_runtime/script_task_canonical_codec.h"
#include "app_runtime/script_task_canonical_renderer.h"
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
inline void run() {
    using Status = ScriptTaskAppFrameCodecStatus;
    assert(script_task_app_frame_codec_supports_version(9) == detail::canonical::available);
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API
    ScriptTaskAppFrameCodecOptions options;
    options.image_provider_id = (1ULL << 48) + 81;
    options.version = 9; options.max_commands = 16; options.max_text_bytes = 1024;
    options.max_payload_bytes = 8192; options.max_groups = 8; options.max_group_depth = 8;
    options.max_input_targets = 4; options.max_clips = 4;
    LayerNode root; root.type = LayerType::Root; root.bounds = {0, 0, 80, 80};
    root.reasons = LayerReasonRoot; root.source_order = 123;
    for (unsigned kind = 0; kind < 10; ++kind) {
        DisplayCommand c; assert(detail::canonical::set_kind(c, kind));
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
                assert(encode_prepared_text(&g, 1, 16125, 1, c.text));
                assert(c.text.size() == 84 && c.text[4] == 1);
            }
        } else if (kind == 8) {
            auto& p = c.image_paint(); p.image_handle = 0xfedcba98U; p.object_fit = ObjectFit::Cover;
            p.object_position = {-13, 157}; p.image_rendering = ImageRendering::CrispEdges; p.border_radius = 5;
            detail::set_command_background_paint_clip(c, {-32768, 32767, 65535, 1});
        } else {
            auto& p = c.css_shadow_paint(); p.border_box = {1, 2, 30, 40}; p.padding_box = {3, 4, 26, 36};
            p.source_rect = {2, 3, 27, 37}; p.border_radii = 2; p.padding_radii = 1; p.source_radii = 2;
            p.blur_px = 0; p.mode = CssBoxShadowMode::Inset; p.flags = 0;
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
    assert(make_script_task_group_frame(root, root.bounds, options, frame) == Status::Accepted);
    assert(frame.groups.size() == 2 && frame.display_list.size() == 10);
    frame.clips.push_back({{0, 0, 60, 60}, 3, kScriptTaskNoParentClip});
    frame.input_targets.push_back({117, {1, 2, 10, 12}, true, 0});
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, options, bytes) == Status::Accepted);
    const std::size_t command_at = 52 + 28 + 2 * 112;
    assert(bytes.size() == command_at + 10 * 128 + 18 + 84 + 24);
    assert(bytes[4] == 9 && detail::canonical::u32(bytes, command_at) == 0);
    // Stable kind7 and a genuine JFTP1 packet; resource identity keeps all64 bits.
    const auto prepared_at = command_at + 7 * 128 + 18;
    assert(detail::canonical::u32(bytes, prepared_at) == 7);
    assert(detail::canonical::i32(bytes, prepared_at + 92) == -123);
    assert(detail::canonical::i32(bytes, prepared_at + 96) == 456);
    assert(bytes[prepared_at + 128 + 4] == 1);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, options, decoded) == Status::Accepted);
    for (std::size_t i = 0; i < frame.display_list.size(); ++i)
        assert(display_commands_equal(frame.display_list[i], decoded.display_list[i]));
    PreparedTextView view; PreparedTextGlyph glyph;
    assert(decode_prepared_text(decoded.display_list[7].text, view) && view.read(0, glyph));
    assert(glyph.resource_id == (1ULL << 48) + 17);
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
    unsupported = frame; unsupported.display_list[7].text[4] = 3;
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
    rendering();
#else
    std::cout << "canonical v9 unavailable on selected Core; legacy contracts retained\n";
#endif
}
} // namespace jellyframe::canonical_codec_tests
