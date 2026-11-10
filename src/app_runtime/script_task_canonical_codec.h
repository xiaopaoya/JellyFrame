#pragma once

#include "app_runtime/script_task_command_access.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_sampling.h"
#include "app_runtime/script_task_prepared_text.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// This is a separate codec family, not a capability declaration for v5-v8.
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API && \
    defined(JELLYFRAME_RENDER_CORE_CANONICAL_LAYER_API_VERSION) && JELLYFRAME_RENDER_CORE_CANONICAL_LAYER_API_VERSION == 1
#define JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API 1
#else
#define JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API 0
#endif

namespace jellyframe::detail::canonical {
using Status = ScriptTaskAppFrameCodecStatus;
constexpr std::size_t header_bytes = 52, group_bytes = 112, command_bytes = 128;
constexpr std::size_t clip_bytes = 28, target_bytes = 24;
constexpr bool available = JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API &&
    has_typed_command<DisplayCommand>::value && has_background_clip_transport<DisplayCommand>();

inline void u32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
inline std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t at) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(b[at + i]) << (8 * i);
    return value;
}
inline int i32(const std::vector<std::uint8_t>& b, std::size_t at) {
    const auto value = u32(b, at);
    return value <= 0x7fffffffU ? static_cast<int>(value) :
        static_cast<int>(static_cast<std::int64_t>(value) - 4294967296LL);
}
inline void f32(std::vector<std::uint8_t>& b, std::size_t at, float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    std::uint32_t bits; std::memcpy(&bits, &value, 4); u32(b, at, bits);
}
inline float f32(const std::vector<std::uint8_t>& b, std::size_t at) {
    const auto bits = u32(b, at); float value; std::memcpy(&value, &bits, 4); return value;
}
inline void rect(std::vector<std::uint8_t>& b, std::size_t at, Rect r) {
    u32(b, at, r.x); u32(b, at + 4, r.y); u32(b, at + 8, r.width); u32(b, at + 12, r.height);
}
inline Rect rect(const std::vector<std::uint8_t>& b, std::size_t at) {
    return {i32(b, at), i32(b, at + 4), i32(b, at + 8), i32(b, at + 12)};
}
inline bool valid_rect(Rect r) { return r.width >= 0 && r.height >= 0; }
inline bool zero(const std::vector<std::uint8_t>& b, std::size_t at, std::size_t count) {
    return std::all_of(b.begin() + at, b.begin() + at + count, [](auto v) { return v == 0; });
}
inline bool add(std::size_t& total, std::size_t count, std::size_t unit = 1) {
    if (count > (std::numeric_limits<std::size_t>::max() - total) / unit) return false;
    total += count * unit; return true;
}
inline bool valid_transform(const DisplayCommandTransform& t) {
    constexpr std::int64_t maximum = 64LL * 1024 * 1024;
    for (auto value : {t.xx_1024, t.xy_1024, t.yx_1024, t.yy_1024, t.tx_1024, t.ty_1024})
        if (value < -maximum || value > maximum) return false;
    return t.enabled || (t.xx_1024 == 1024 && t.xy_1024 == 0 && t.yx_1024 == 0 &&
        t.yy_1024 == 1024 && t.tx_1024 == 0 && t.ty_1024 == 0 && t.source_clip_index == kScriptTaskNoClip);
}
inline bool valid_clip(CommandBackgroundPaintClip p) {
    return command_background_paint_clip_empty(p) || (p.dx >= -32768 && p.dx <= 32767 &&
        p.dy >= -32768 && p.dy <= 32767 && p.width > 0 && p.width <= 65535 && p.height > 0 && p.height <= 65535);
}
inline int layer_kind(LayerType type) {
    switch (type) {
    case LayerType::Root: return 0; case LayerType::Paint: return 1;
    case LayerType::Clip: return 2; case LayerType::Stacking: return 3;
    case LayerType::Composited: return 4; default: return -1;
    }
}
inline bool set_layer_kind(ScriptTaskFrameGroup& g, unsigned value) {
    switch (value) {
    case 0: g.type = LayerType::Root; break; case 1: g.type = LayerType::Paint; break;
    case 2: g.type = LayerType::Clip; break; case 3: g.type = LayerType::Stacking; break;
    case 4: g.type = LayerType::Composited; break; default: return false;
    }
    return true;
}
inline void paint_clip(std::vector<std::uint8_t>& b, std::size_t at, CommandBackgroundPaintClip p) {
    const int values[] = {p.dx, p.dy, p.width, p.height};
    for (unsigned i = 0; i < 4; ++i) {
        const auto v = static_cast<std::uint16_t>(values[i]);
        b[at + 2 * i] = static_cast<std::uint8_t>(v); b[at + 2 * i + 1] = static_cast<std::uint8_t>(v >> 8);
    }
}
inline CommandBackgroundPaintClip paint_clip(const std::vector<std::uint8_t>& b, std::size_t at) {
    int v[4]; for (unsigned i = 0; i < 4; ++i) v[i] = b[at + 2 * i] | (int(b[at + 2 * i + 1]) << 8);
    return {v[0] < 32768 ? v[0] : v[0] - 65536, v[1] < 32768 ? v[1] : v[1] - 65536, v[2], v[3]};
}

inline Status validate_groups(const ScriptTaskAppFrame& frame, std::size_t max_groups, std::size_t max_depth) {
    if (frame.groups.empty()) return Status::InvalidGroup;
    if (frame.groups.size() > max_groups || frame.groups.size() > UINT32_MAX) return Status::TooManyGroups;
    if (!frame.command_ellipses.empty() || !frame.display_clip_indices.empty()) return Status::UnsupportedGroupFeature;
    std::vector<std::uint32_t> ancestors;
    std::size_t command = 0;
    for (std::size_t index = 0; index < frame.groups.size(); ++index) {
        const auto& g = frame.groups[index];
        if (index == 0) { if (g.parent != kScriptTaskNoParentClip) return Status::InvalidGroup; }
        else {
            while (!ancestors.empty() && ancestors.back() != g.parent) ancestors.pop_back();
            if (ancestors.empty()) return Status::InvalidGroup;
        }
        if (ancestors.size() >= max_depth) return Status::InvalidGroup;
        ancestors.push_back(static_cast<std::uint32_t>(index));
        if (g.clip_axes != 3 || g.has_affine || g.has_clip_ellipse ||
            g.affine != std::array<float, 6>{{1, 0, 0, 1, 0, 0}} ||
            g.clip_ellipse != std::array<int, 8>{}) return Status::UnsupportedGroupFeature;
        if (g.command_begin != command || command > frame.display_list.size() ||
            g.command_count > frame.display_list.size() - command || !valid_rect(g.bounds) || !valid_rect(g.clip_rect) ||
            layer_kind(g.type) < 0 || (g.reasons & ~(LayerReasonRoot | LayerReasonOverflowClip | LayerReasonOpacity |
                LayerReasonTransform | LayerReasonPositioned | LayerReasonZIndex | LayerReasonShadow |
                LayerReasonRoundedClip | LayerReasonTransientOverlay)) != 0 ||
            !std::isfinite(g.opacity) || g.opacity < 0 || g.opacity > 1 ||
            !std::isfinite(g.transform.translate_x) || !std::isfinite(g.transform.translate_y) ||
            !std::isfinite(g.transform.scale_x) || !std::isfinite(g.transform.scale_y) ||
            !std::isfinite(g.transform.rotate_degrees) || g.source_order > std::numeric_limits<std::size_t>::max())
            return Status::InvalidGroup;
        command += g.command_count;
    }
    return command == frame.display_list.size() ? Status::Accepted : Status::InvalidGroup;
}

#if JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API
static_assert(kPreparedTextHeaderBytes == 32 && kPreparedTextRecordBytes == 52,
              "v9 requires the actual canonical JFTP1 contract");

// Stable canonical wire kinds; never serialize a Core enum's underlying value.
template<class E> inline int kind_impl(E t) {
    switch (t) {
    case DisplayCommandType::FillRect: return 0; case DisplayCommandType::StrokeRect: return 1;
    case DisplayCommandType::LinearGradient: return 2; case DisplayCommandType::ConicGradient: return 3;
    case DisplayCommandType::RadialGradient: return 4; case DisplayCommandType::BoxShadow: return 5;
    case DisplayCommandType::Text: return 6; case DisplayCommandType::PreparedText: return 7;
    case DisplayCommandType::Image: return 8;
    default: return -1;
    }
}
template<class E> inline int kind_shadow(E t) {
    if constexpr (has_css_shadow_type<E>::value) {
        if (t == E::CssBoxShadow) return 9;
        return kind_impl(static_cast<DisplayCommandType>(t));
    }
    else return kind_impl(static_cast<DisplayCommandType>(t));
}
template<class C> inline int command_kind(const C& c) {
    const auto t = command_type(c);
    return kind_shadow(t);
}
template<class C> inline bool set_kind_impl(C& c, unsigned value) {
    using E = decltype(command_type(c));
    switch (value) {
    case 0: set_command_type(c, E::FillRect); break;
    case 1: set_command_type(c, E::StrokeRect); break;
    case 2: set_command_type(c, E::LinearGradient); break;
    case 3: set_command_type(c, E::ConicGradient); break;
    case 4: set_command_type(c, E::RadialGradient); break;
    case 5: set_command_type(c, E::BoxShadow); break;
    case 6: set_command_type(c, E::Text); break;
    case 7: if constexpr (has_prepared_command_type<E>::value) set_command_type(c, E::PreparedText); else return false; break;
    case 8: set_command_type(c, E::Image); break;
    case 9:
        if constexpr (has_css_shadow_type<E>::value) set_command_type(c, E::CssBoxShadow);
        else return false;
        break;
    default: return false;
    }
    return true;
}
inline bool set_kind(DisplayCommand& c, unsigned value) { return set_kind_impl(c, value); }
template<class C> inline bool valid_command(const C& c) {
    const auto k = command_kind(c);
    if (k < 0 || !valid_rect(c.rect) || !valid_transform(c.transform) ||
        c.text.size() > UINT32_MAX) return false;
    if (k <= 5) {
        const auto p = legacy_command_fields(c);
        if (static_cast<unsigned>(p.gradient_axis) > 4 || !valid_clip(command_background_paint_clip(c))) return false;
    } else if (k <= 7) {
        const auto p = legacy_command_fields(c);
        if (static_cast<unsigned>(p.text_align) > 2) return false;
        if (k == 7) { PreparedTextView view; if (!decode_prepared_text(c.text, view)) return false; }
    } else if (k == 8) {
        const auto p = legacy_command_fields(c);
        if (static_cast<unsigned>(p.object_fit) > 4 || static_cast<unsigned>(p.image_rendering) > 2 ||
            !valid_clip(command_background_paint_clip(c))) return false;
    } else {
      if constexpr (has_css_shadow_paint<C>::value) {
        const auto& p = c.css_shadow_paint();
        if (!valid_rect(p.border_box) || !valid_rect(p.padding_box) || !valid_rect(p.source_rect) ||
            static_cast<unsigned>(p.mode) > 1 || (p.flags & ~1U)) return false;
      } else return false;
    }
    // Non-text canonical paints never use ABI9 fractional/extension packets.
    return (k == 6 || k == 7) || c.text.empty();
}

template<class C> inline void put_command(std::vector<std::uint8_t>& b, std::size_t at, const C& c) {
    const auto k = command_kind(c); u32(b, at, k); rect(b, at + 4, c.rect);
    const Color colors[] = {c.color, c.color2};
    for (unsigned i = 0; i < 2; ++i) {
        b[at + 20 + 4 * i] = colors[i].r; b[at + 21 + 4 * i] = colors[i].g;
        b[at + 22 + 4 * i] = colors[i].b; b[at + 23 + 4 * i] = colors[i].a;
    }
    u32(b, at + 28, static_cast<std::uint32_t>(c.text.size()));
    u32(b, at + 32, c.transform.enabled); u32(b, at + 36, c.transform.xx_1024);
    u32(b, at + 40, c.transform.xy_1024); u32(b, at + 44, c.transform.yx_1024);
    u32(b, at + 48, c.transform.yy_1024); u32(b, at + 52, c.transform.tx_1024);
    u32(b, at + 56, c.transform.ty_1024); u32(b, at + 60, c.transform.source_clip_index);
    at += 64;
    if (k <= 5) {
        const auto p = legacy_command_fields(c); u32(b, at, p.border_radius); u32(b, at + 4, p.stroke_width);
        u32(b, at + 8, static_cast<unsigned>(p.gradient_axis)); u32(b, at + 12, p.gradient_stop_percent);
        paint_clip(b, at + 16, command_background_paint_clip(c));
    } else if (k <= 7) {
        const auto p = legacy_command_fields(c);
        u32(b, at, p.border_radius); u32(b, at + 4, p.stroke_width); u32(b, at + 8, p.font_size);
        u32(b, at + 12, p.font_weight); u32(b, at + 16, p.font_family_hash);
        u32(b, at + 20, static_cast<unsigned>(p.text_align)); u32(b, at + 24, p.text_single_line);
        if constexpr (has_typed_command<C>::value) {
            const auto& text = c.text_paint();
            u32(b, at + 28, text.origin_x); u32(b, at + 32, text.baseline_y);
        } else {
            u32(b, at + 28, c.object_position.x_percent); u32(b, at + 32, c.object_position.y_percent);
        }
    } else if (k == 8) {
        const auto p = legacy_command_fields(c); u32(b, at, p.image_handle);
        paint_clip(b, at + 4, command_background_paint_clip(c));
        u32(b, at + 12, static_cast<unsigned>(p.object_fit)); u32(b, at + 16, p.object_position.x_percent);
        u32(b, at + 20, p.object_position.y_percent); u32(b, at + 24, static_cast<unsigned>(p.image_rendering));
        u32(b, at + 28, p.border_radius);
    } else {
      if constexpr (has_css_shadow_paint<C>::value) {
        const auto& p = c.css_shadow_paint(); rect(b, at, p.border_box); rect(b, at + 16, p.padding_box);
        rect(b, at + 32, p.source_rect); u32(b, at + 48, p.border_radii); u32(b, at + 52, p.padding_radii);
        u32(b, at + 56, p.source_radii); b[at + 60] = static_cast<std::uint8_t>(p.blur_px);
        b[at + 61] = static_cast<std::uint8_t>(p.blur_px >> 8); b[at + 62] = static_cast<std::uint8_t>(p.mode);
        b[at + 63] = p.flags;
      }
    }
}
template<class C> inline bool get_command(const std::vector<std::uint8_t>& b, std::size_t at, C& c) {
    const auto k = u32(b, at);
    if (!set_kind_impl(c, k) || u32(b, at + 32) > 1 || u32(b, at + 60) > 65535) return false;
    c.rect = rect(b, at + 4); c.color = {b[at+20], b[at+21], b[at+22], b[at+23]};
    c.color2 = {b[at+24], b[at+25], b[at+26], b[at+27]};
    c.transform.enabled = u32(b, at + 32) != 0;
    c.transform.xx_1024 = i32(b, at + 36); c.transform.xy_1024 = i32(b, at + 40);
    c.transform.yx_1024 = i32(b, at + 44); c.transform.yy_1024 = i32(b, at + 48);
    c.transform.tx_1024 = i32(b, at + 52); c.transform.ty_1024 = i32(b, at + 56);
    c.transform.source_clip_index = static_cast<std::uint16_t>(u32(b, at + 60)); at += 64;
    if (k <= 5) {
        if (u32(b, at + 8) > 4 || !zero(b, at + 24, 40)) return false;
        auto p = legacy_command_fields(c); p.border_radius = i32(b, at); p.stroke_width = i32(b, at + 4);
        p.gradient_axis = static_cast<GradientAxis>(u32(b, at + 8)); p.gradient_stop_percent = i32(b, at + 12);
        const auto q = paint_clip(b, at + 16);
        apply_legacy_command_fields(c, p); set_command_background_paint_clip(c, q);
    } else if (k <= 7) {
        if (u32(b, at + 20) > 2 || u32(b, at + 24) > 1 || !zero(b, at + 36, 28)) return false;
        auto p = legacy_command_fields(c); p.border_radius = i32(b, at); p.stroke_width = i32(b, at + 4);
        p.font_size = i32(b, at + 8); p.font_weight = i32(b, at + 12); p.font_family_hash = u32(b, at + 16);
        p.text_align = static_cast<TextCommandAlign>(u32(b, at + 20)); p.text_single_line = u32(b, at + 24) != 0;
        apply_legacy_command_fields(c, p);
        if constexpr (has_typed_command<C>::value) {
            c.text_paint().origin_x = i32(b, at + 28);
            c.text_paint().baseline_y = i32(b, at + 32);
        } else c.object_position = {i32(b, at + 28), i32(b, at + 32)};
    } else if (k == 8) {
        if (u32(b, at + 12) > 4 || u32(b, at + 24) > 2 || !zero(b, at + 32, 32)) return false;
        auto p = legacy_command_fields(c); p.image_handle = u32(b, at); const auto q = paint_clip(b, at + 4);
        p.object_fit = static_cast<ObjectFit>(u32(b, at + 12));
        p.object_position = {i32(b, at + 16), i32(b, at + 20)};
        p.image_rendering = static_cast<ImageRendering>(u32(b, at + 24)); p.border_radius = i32(b, at + 28);
        apply_legacy_command_fields(c, p); set_command_background_paint_clip(c, q);
    } else {
      if constexpr (has_css_shadow_paint<C>::value) {
        if (b[at + 62] > 1 || b[at + 63] > 1) return false;
        auto& p = c.css_shadow_paint(); p.border_box = rect(b, at); p.padding_box = rect(b, at + 16);
        p.source_rect = rect(b, at + 32); p.border_radii = i32(b, at + 48); p.padding_radii = i32(b, at + 52);
        p.source_radii = i32(b, at + 56); p.blur_px = static_cast<std::uint16_t>(b[at+60] | (unsigned(b[at+61]) << 8));
        p.mode = static_cast<CssBoxShadowMode>(b[at + 62]); p.flags = b[at + 63];
      } else return false;
    }
    return true;
}

inline void put_group(std::vector<std::uint8_t>& b, std::size_t at, const ScriptTaskFrameGroup& g) {
    u32(b, at, g.parent); u32(b, at + 4, g.command_begin); u32(b, at + 8, g.command_count);
    u32(b, at + 12, layer_kind(g.type)); rect(b, at + 16, g.bounds); rect(b, at + 32, g.clip_rect);
    u32(b, at + 48, g.clip_border_radius); u32(b, at + 52, unsigned(g.has_clip) | (unsigned(g.has_transform) << 1));
    f32(b, at + 56, g.opacity); f32(b, at + 60, g.transform.translate_x); f32(b, at + 64, g.transform.translate_y);
    f32(b, at + 68, g.transform.scale_x); f32(b, at + 72, g.transform.scale_y); f32(b, at + 76, g.transform.rotate_degrees);
    u32(b, at + 80, g.origin_x_percent); u32(b, at + 84, g.origin_y_percent); u32(b, at + 88, g.reasons);
    u32(b, at + 92, g.scroll_y); u32(b, at + 96, g.max_scroll_y); u32(b, at + 100, g.z_index);
    u32(b, at + 104, static_cast<std::uint32_t>(g.source_order)); u32(b, at + 108, static_cast<std::uint32_t>(g.source_order >> 32));
}
inline bool get_group(const std::vector<std::uint8_t>& b, std::size_t at, ScriptTaskFrameGroup& g) {
    if (!set_layer_kind(g, u32(b, at + 12)) || u32(b, at + 52) > 3) return false;
    g.parent = u32(b, at); g.command_begin = u32(b, at + 4); g.command_count = u32(b, at + 8);
    g.bounds = rect(b, at + 16); g.clip_rect = rect(b, at + 32);
    g.clip_border_radius = i32(b, at + 48); g.has_clip = (u32(b, at + 52) & 1) != 0;
    g.has_transform = (u32(b, at + 52) & 2) != 0; g.opacity = f32(b, at + 56);
    g.transform = {f32(b, at + 60), f32(b, at + 64), f32(b, at + 68), f32(b, at + 72), f32(b, at + 76)};
    g.origin_x_percent = i32(b, at + 80); g.origin_y_percent = i32(b, at + 84); g.reasons = u32(b, at + 88);
    g.scroll_y = i32(b, at + 92); g.max_scroll_y = i32(b, at + 96); g.z_index = i32(b, at + 100);
    g.source_order = u32(b, at + 104) | (std::uint64_t(u32(b, at + 108)) << 32); return true;
}

inline Status validate(const ScriptTaskAppFrame& f, const ScriptTaskAppFrameCodecOptions& o, std::size_t& total) {
    if (!valid_rect(f.viewport)) return Status::InvalidValue;
    if (f.clip_metadata_overflow) return Status::InvalidClip;
    if (f.display_list.size() > o.max_commands || f.display_list.size() > UINT32_MAX) return Status::TooManyCommands;
    if (f.input_targets.size() > o.max_input_targets || f.input_targets.size() > UINT32_MAX) return Status::TooManyInputTargets;
    if (f.clips.size() > o.max_clips || f.clips.size() > 65535) return Status::TooManyClips;
    auto status = validate_groups(f, o.max_groups, o.max_group_depth); if (status != Status::Accepted) return status;
    std::vector<std::size_t> depths;
    for (const auto& c : f.clips) {
        const auto index = depths.size();
        if (!valid_rect(c.rect) || (c.parent_clip != kScriptTaskNoParentClip && c.parent_clip >= index)) return Status::InvalidClip;
        const auto depth = c.parent_clip == kScriptTaskNoParentClip ? 1 : depths[c.parent_clip] + 1;
        if (depth > o.max_clip_depth) return Status::TooDeepClipChain;
        depths.push_back(depth);
    }
    total = header_bytes;
    if (!add(total, f.clips.size(), clip_bytes) || !add(total, f.groups.size(), group_bytes) ||
        !add(total, f.display_list.size(), command_bytes) || !add(total, f.input_targets.size(), target_bytes))
        return Status::PayloadTooLarge;
    std::size_t text = 0;
    for (const auto& c : f.display_list) {
        if (!valid_command(c)) return Status::InvalidValue;
        // Group commands remain in local source coordinates. A second command
        // transform would not be applied by the canonical compositor.
        if (c.transform.enabled || c.transform.source_clip_index != kScriptTaskNoClip) return Status::UnsupportedTransformFeature;
        if (!add(text, c.text.size()) || text > o.max_text_bytes) return Status::TooManyTextBytes;
        if (!add(total, c.text.size())) return Status::PayloadTooLarge;
    }
    const bool has_images = std::any_of(f.display_list.begin(), f.display_list.end(), [](const auto& c) { return c.type() == DisplayCommandType::Image; });
    if (has_images != (f.image_provider_id != 0)) return Status::InvalidValue;
    for (std::size_t i = 0; i < f.input_targets.size(); ++i) {
        const auto& t = f.input_targets[i];
        if (!valid_rect(t.rect)) return Status::InvalidValue;
        if (t.clip_index != kScriptTaskNoClip && t.clip_index >= f.clips.size()) return Status::InvalidClip;
        for (std::size_t j = 0; j < i; ++j)
            if (f.input_targets[j].target_key == t.target_key) return Status::InvalidValue;
    }
    return total <= o.max_payload_bytes ? Status::Accepted : Status::PayloadTooLarge;
}

inline Status encode(const ScriptTaskAppFrame& f, const ScriptTaskAppFrameCodecOptions& o, std::vector<std::uint8_t>& output) {
    std::size_t total; auto status = validate(f, o, total); if (status != Status::Accepted) return status;
    std::vector<std::uint8_t> b(total, 0); u32(b, 0, 0x4653464aU); b[4] = 9; rect(b, 8, f.viewport);
    u32(b, 24, static_cast<std::uint32_t>(f.display_list.size())); u32(b, 28, static_cast<std::uint32_t>(f.input_targets.size()));
    u32(b, 32, static_cast<std::uint32_t>(f.clips.size())); u32(b, 36, static_cast<std::uint32_t>(f.groups.size()));
    u32(b, 44, static_cast<std::uint32_t>(f.image_provider_id)); u32(b, 48, static_cast<std::uint32_t>(f.image_provider_id >> 32));
    std::size_t at = header_bytes;
    for (const auto& c : f.clips) { rect(b, at, c.rect); u32(b, at + 16, c.border_radius); u32(b, at + 20, c.parent_clip); at += clip_bytes; }
    for (const auto& g : f.groups) { put_group(b, at, g); at += group_bytes; }
    for (const auto& c : f.display_list) {
        put_command(b, at, c); at += command_bytes;
        std::copy(c.text.begin(), c.text.end(), b.begin() + at); at += c.text.size();
    }
    for (const auto& t : f.input_targets) {
        u32(b, at, t.target_key); rect(b, at + 4, t.rect); b[at + 20] = t.enabled;
        b[at + 21] = static_cast<std::uint8_t>(t.clip_index); b[at + 22] = static_cast<std::uint8_t>(t.clip_index >> 8); at += target_bytes;
    }
    output = std::move(b); return Status::Accepted;
}

inline Status decode(const std::vector<std::uint8_t>& b, const ScriptTaskAppFrameCodecOptions& o, ScriptTaskAppFrame& output) {
    if (o.version != 9) return Status::UnsupportedVersion;
    if (b.size() < header_bytes || b.size() > o.max_payload_bytes || u32(b, 0) != 0x4653464aU || b[4] != 9 ||
        !zero(b, 5, 3) || u32(b, 40) != 0) return Status::Malformed;
    const auto commands = u32(b, 24), targets = u32(b, 28), clips = u32(b, 32), groups = u32(b, 36);
    if (commands > o.max_commands) return Status::TooManyCommands;
    if (targets > o.max_input_targets) return Status::TooManyInputTargets;
    if (clips > o.max_clips || clips > 65535) return Status::TooManyClips;
    if (groups > o.max_groups) return Status::TooManyGroups;
    std::size_t minimum = header_bytes;
    if (!add(minimum, commands, command_bytes) || !add(minimum, targets, target_bytes) ||
        !add(minimum, clips, clip_bytes) || !add(minimum, groups, group_bytes) || minimum > b.size()) return Status::Malformed;
    ScriptTaskAppFrame f; f.viewport = rect(b, 8); f.image_provider_id = u32(b, 44) | (std::uint64_t(u32(b, 48)) << 32); std::size_t at = header_bytes;
    for (std::uint32_t i = 0; i < clips; ++i) {
        if (!zero(b, at + 24, 4)) return Status::Malformed;
        f.clips.push_back({rect(b, at), i32(b, at + 16), u32(b, at + 20)}); at += clip_bytes;
    }
    for (std::uint32_t i = 0; i < groups; ++i) {
        ScriptTaskFrameGroup g; if (!get_group(b, at, g)) return Status::Malformed;
        f.groups.push_back(g); at += group_bytes;
    }
    std::size_t text = 0;
    for (std::uint32_t i = 0; i < commands; ++i) {
        if (b.size() - at < command_bytes) return Status::Malformed;
        DisplayCommand c; if (!get_command(b, at, c)) return Status::Malformed;
        const auto length = u32(b, at + 28); at += command_bytes;
        if (length > b.size() - at) return Status::Malformed;
        if (!add(text, length) || text > o.max_text_bytes) return Status::TooManyTextBytes;
        c.text.assign(reinterpret_cast<const char*>(b.data() + at), length); at += length;
        f.display_list.push_back(std::move(c));
    }
    for (std::uint32_t i = 0; i < targets; ++i) {
        if (b.size() - at < target_bytes || b[at + 20] > 1 || b[at + 23] != 0) return Status::Malformed;
        f.input_targets.push_back({u32(b, at), rect(b, at + 4), b[at + 20] != 0,
            static_cast<std::uint16_t>(b[at+21] | (unsigned(b[at+22]) << 8))}); at += target_bytes;
    }
    if (at != b.size()) return Status::Malformed;
    std::size_t total; auto status = validate(f, o, total);
    if (status != Status::Accepted) return status == Status::InvalidValue ? Status::Malformed : status;
    output = std::move(f); return Status::Accepted;
}

inline Status capture(const LayerNode& root, Rect viewport, const ScriptTaskAppFrameCodecOptions& o, ScriptTaskAppFrame& output) {
    ScriptTaskAppFrame f; f.viewport = viewport;
    std::size_t captured_text = 0, captured_bytes = header_bytes;
    struct Cursor { const LayerNode* node; std::uint32_t index; std::size_t child; };
    std::vector<Cursor> pending;
    const auto append = [&](const LayerNode& n, std::uint32_t parent) -> Status {
        if (f.groups.size() >= o.max_groups || f.groups.size() >= UINT32_MAX) return Status::TooManyGroups;
        if (f.display_list.size() > o.max_commands || n.display_list.size() > o.max_commands - f.display_list.size() ||
            n.display_list.size() > UINT32_MAX - f.display_list.size()) return Status::TooManyCommands;
        if (!add(captured_bytes, 1, group_bytes) || !add(captured_bytes, n.display_list.size(), command_bytes) ||
            captured_bytes > o.max_payload_bytes) return Status::PayloadTooLarge;
        for (const auto& c : n.display_list) {
            if (!valid_command(c)) return Status::InvalidValue;
            if (command_kind(c) == 8) f.image_provider_id = o.image_provider_id;
            if (!add(captured_text, c.text.size()) || captured_text > o.max_text_bytes) return Status::TooManyTextBytes;
            if (!add(captured_bytes, c.text.size()) || captured_bytes > o.max_payload_bytes) return Status::PayloadTooLarge;
        }
        ScriptTaskFrameGroup g; g.parent = parent; g.command_begin = static_cast<std::uint32_t>(f.display_list.size());
        g.command_count = static_cast<std::uint32_t>(n.display_list.size()); g.type = n.type; g.bounds = n.bounds;
        g.clip_rect = n.clip_rect; g.clip_border_radius = n.clip_border_radius; g.has_clip = n.has_clip;
        g.opacity = n.opacity; g.transform = n.transform; g.has_transform = n.has_transform;
        g.origin_x_percent = n.transform_origin_x_percent; g.origin_y_percent = n.transform_origin_y_percent;
        g.reasons = n.reasons; g.scroll_y = n.scroll_y; g.max_scroll_y = n.max_scroll_y; g.z_index = n.z_index; g.source_order = n.source_order;
        f.display_list.insert(f.display_list.end(), n.display_list.begin(), n.display_list.end());
        const auto index = static_cast<std::uint32_t>(f.groups.size()); f.groups.push_back(g); pending.push_back({&n, index, 0});
        return Status::Accepted;
    };
    auto status = append(root, kScriptTaskNoParentClip); if (status != Status::Accepted) return status;
    while (!pending.empty()) {
        auto& cursor = pending.back();
        if (cursor.child == cursor.node->children.size()) { pending.pop_back(); continue; }
        if (pending.size() >= o.max_group_depth) return Status::InvalidGroup;
        const auto parent = cursor.index; const auto* child = cursor.node->children[cursor.child++].get();
        if (!child) return Status::InvalidGroup;
        status = append(*child, parent); if (status != Status::Accepted) return status;
    }
    std::size_t total; status = validate(f, o, total); if (status != Status::Accepted) return status;
    output = std::move(f); return Status::Accepted;
}
#else
inline Status encode(const ScriptTaskAppFrame&, const ScriptTaskAppFrameCodecOptions&, std::vector<std::uint8_t>&) { return Status::UnsupportedVersion; }
inline Status decode(const std::vector<std::uint8_t>&, const ScriptTaskAppFrameCodecOptions&, ScriptTaskAppFrame&) { return Status::UnsupportedVersion; }
inline Status capture(const LayerNode&, Rect, const ScriptTaskAppFrameCodecOptions&, ScriptTaskAppFrame&) { return Status::UnsupportedVersion; }
#endif
} // namespace jellyframe::detail::canonical
